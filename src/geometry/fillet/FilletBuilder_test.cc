// Unit tests for the fillet edge-classification internals: MeshGL vertex
// merging, edge -> two-face adjacency, and concave/convex classification. These
// exercise the pure combinatorics directly on hand-built Manifold primitives,
// where the correct answer is a count or a boolean — the cases a rendered image
// reports poorly (a concavity sign flip is invisible until it becomes a gouge; a
// seam that leaks past the angle filter is one stray edge among hundreds).

#include <catch2/catch_all.hpp>

#ifdef ENABLE_MANIFOLD

#include "geometry/fillet/FilletBuilder_internal.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <manifold/manifold.h>

#include "core/CurveDiscretizer.h"
#include "geometry/PolySet.h"

using namespace fillet::detail;
using Catch::Approx;

namespace {

// Classify a manifold at a given crease threshold, mirroring what buildFilletTool
// does minus the logging: merge, rebuild adjacency, tally.
ClassCounts classify(const manifold::Manifold& m, double thresholdDeg)
{
  const MergedMesh mm = mergeMesh(m.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const bool useProvenance = mm.distinctIDs.size() > 1;
  return classifyEdges(mm, adj, thresholdDeg, useProvenance);
}

// An axis-aligned box spanning [0,size].
manifold::Manifold box(double sx, double sy, double sz)
{
  return manifold::Manifold::Cube(manifold::vec3(sx, sy, sz), false);
}

// A CurveDiscretizer standing in for a given set of tessellation variables, the
// way the node's factory builds one from the call's $fn/$fa/$fs.
CurveDiscretizer discretizer(double fn, double fa = 12.0, double fs = 2.0)
{
  return CurveDiscretizer([=](const char *name) -> std::optional<double> {
    const std::string n = name;
    if (n == "fn") return fn;
    if (n == "fa") return fa;
    if (n == "fs") return fs;
    return std::nullopt;
  });
}

// The crease threshold the operator derives from the tessellation parameters:
// half again the coarsest seam OpenSCAD would generate at those settings.
double derivedThreshold(const CurveDiscretizer& d) { return 1.5 * d.getMaxSeamAngle(); }

// A gable prism: a 60 x 10 slab with a roof over it, apex at (30, 18), extruded
// 40 in z. The two shoulders turn 75 degrees and the apex turns 30, so one solid
// carries a steep crease and a shallow one and a threshold can fall between
// them. Both are real features of the shape; neither is a tessellation seam.
manifold::Manifold roofPrism()
{
  const manifold::Polygons section{{{0, 0}, {60, 0}, {60, 10}, {30, 18}, {0, 10}}};
  return manifold::Manifold::Extrude(section, 40.0);
}

// Is the edge between two named points selected by this tool at this threshold?
// Positions rather than indices, because merging renumbers the vertices.
bool isSelected(const manifold::Manifold& m, double thresholdDeg, const Vector3d& a,
                const Vector3d& b, bool wantConcave = false)
{
  const MergedMesh mm = mergeMesh(m.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  for (const auto& key : selectedEdges(mm, adj, thresholdDeg, wantConcave)) {
    const Vector3d& p = mm.pos[key.first];
    const Vector3d& q = mm.pos[key.second];
    if (((p - a).norm() < 1e-6 && (q - b).norm() < 1e-6) ||
        ((p - b).norm() < 1e-6 && (q - a).norm() < 1e-6))
      return true;
  }
  return false;
}

}  // namespace

TEST_CASE("cube: vertex merge and edge adjacency")
{
  const auto cube = manifold::Manifold::Cube(manifold::vec3(1.0), false);
  const MergedMesh mm = mergeMesh(cube.GetMeshGL64());

  // A cube is 8 corners and 12 triangles regardless of how MeshGL duplicated
  // vertices across runs.
  CHECK(mm.pos.size() == 8);
  CHECK(mm.tris.size() == 12);

  const auto adj = buildEdgeAdjacency(mm.tris);

  // 12 cube edges + 6 face diagonals from triangulating the six quads = 18
  // undirected edges, every one shared by exactly two triangles.
  CHECK(adj.size() == 18);
  size_t nonTwoFace = 0;
  for (const auto& [key, ts] : adj) {
    if (ts.size() != 2) ++nonTwoFace;
  }
  CHECK(nonTwoFace == 0);
}

TEST_CASE("cube: all feature edges are convex and diagonals rejected as seams")
{
  const auto cube = manifold::Manifold::Cube(manifold::vec3(1.0), false);
  const ClassCounts c = classify(cube, 45.0);

  CHECK(c.nonManifold == 0);
  CHECK(c.twoFace == 18);
  // The 6 coplanar face diagonals sit at 0 degrees and fall below threshold; the
  // 12 real edges are all convex on a convex solid.
  CHECK(c.feature == 12);
  CHECK(c.featureConvex == 12);
  CHECK(c.featureConcave == 0);
}

TEST_CASE("L-shape: the reflex corner is the one concave feature edge")
{
  // Two overlapping bars forming an L in the xy plane, extruded in z. The inner
  // vertical edge at the reflex corner is concave; every other feature edge is
  // convex.
  const auto barX = manifold::Manifold::Cube(manifold::vec3(2.0, 1.0, 1.0), false);
  const auto barY = manifold::Manifold::Cube(manifold::vec3(1.0, 2.0, 1.0), false);
  const auto ell = barX + barY;

  const ClassCounts c = classify(ell, 45.0);

  CHECK(c.nonManifold == 0);
  // Exactly one concave crease — the reflex vertical edge — is the whole point:
  // it is what fillet_tool selects. The convex tally is left loose because the
  // union leaves collinear T-vertices along the seam (each bar's edges stop at
  // the overlap and split the other's longer edges), so the outer edges come
  // back as several collinear convex segments rather than a fixed count.
  CHECK(c.featureConcave == 1);
  CHECK(c.featureConvex >= 5);
  CHECK(c.feature == c.featureConcave + c.featureConvex);
}

TEST_CASE("cylinder: classification is stable across tessellations")
{
  // The top and bottom rim edges are 90-degree creases at every $fn; the side
  // seams (360/$fn) and the coplanar cap fan sit below a 60-degree threshold and
  // must always be rejected. So feature edges == 2*$fn, all convex, at every
  // resolution — the count scales but the character does not.
  for (const int fn : {8, 16, 64}) {
    CAPTURE(fn);
    const auto cyl = manifold::Manifold::Cylinder(1.0, 1.0, 1.0, fn, false);
    const ClassCounts c = classify(cyl, 60.0);

    CHECK(c.nonManifold == 0);
    CHECK(c.feature == static_cast<size_t>(2 * fn));
    CHECK(c.featureConvex == static_cast<size_t>(2 * fn));
    CHECK(c.featureConcave == 0);
  }
}

TEST_CASE("cylinder: convex rims walk into two closed rings")
{
  const int fn = 24;
  const auto cyl = manifold::Manifold::Cylinder(10.0, 6.0, 6.0, fn, false);
  const MergedMesh mm = mergeMesh(cyl.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);

  // round_tool acts on convex edges; the only convex creases are the two rims.
  const auto edges = selectedEdges(mm, adj, 60.0, /*wantConcave=*/false);
  CHECK(edges.size() == static_cast<size_t>(2 * fn));

  const auto chains = buildChains(mm, edges);
  REQUIRE(chains.size() == 2);
  for (const auto& ch : chains) {
    CHECK(ch.closed);
    CHECK(ch.verts.size() == static_cast<size_t>(fn));
  }
}

TEST_CASE("L-shape: the concave crease is a single open chain")
{
  const auto ell = box(2.0, 1.0, 1.0) + box(1.0, 2.0, 1.0);
  const MergedMesh mm = mergeMesh(ell.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);

  const auto edges = selectedEdges(mm, adj, 45.0, /*wantConcave=*/true);
  REQUIRE(edges.size() == 1);

  const auto chains = buildChains(mm, edges);
  REQUIRE(chains.size() == 1);
  CHECK_FALSE(chains[0].closed);
  CHECK(chains[0].verts.size() == 2);
}

TEST_CASE("floor/wall: tangency frame matches the worked example")
{
  // Floor slab (top at z=1) meeting a wall (right face at x=1) along y: the
  // classic inner 90-degree corner. The concave crease sits on the line
  // x=1, z=1; at radius r the ball center is r*sqrt(2) up the diagonal and the
  // two tangency points land on the floor and the wall.
  const double r = 3.0;
  const auto model = box(10.0, 10.0, 1.0) + box(1.0, 10.0, 10.0);
  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);

  const auto edges = selectedEdges(mm, adj, 45.0, /*wantConcave=*/true);
  REQUIRE(edges.size() == 1);
  const auto chains = buildChains(mm, edges);
  REQUIRE(chains.size() == 1);

  const auto frames = spineFrames(mm, adj, chains[0], r, /*concave=*/true);
  REQUIRE(frames.size() >= 2);

  int validCount = 0;
  for (const auto& f : frames) {
    if (!f.valid) continue;
    ++validCount;
    CHECK(f.phiDeg == Approx(90.0).margin(1e-6));

    // The crease vertex lies on x=1, z=1.
    CHECK(f.v.x() == Approx(1.0).margin(1e-9));
    CHECK(f.v.z() == Approx(1.0).margin(1e-9));

    // Ball center: r*sqrt(2) along the (x,z) diagonal from the vertex.
    CHECK((f.C - f.v).x() == Approx(r).margin(1e-6));
    CHECK((f.C - f.v).y() == Approx(0.0).margin(1e-6));
    CHECK((f.C - f.v).z() == Approx(r).margin(1e-6));

    // Tangency points land one on the floor (offset (r,0,0)) and one on the
    // wall (offset (0,0,r)); which is TA vs TB depends on side ordering, so
    // check the unordered pair.
    const Vector3d ta = f.TA - f.v;
    const Vector3d tb = f.TB - f.v;
    const bool taFloor = ta.isApprox(Vector3d(r, 0, 0), 1e-6) &&
                         tb.isApprox(Vector3d(0, 0, r), 1e-6);
    const bool taWall = ta.isApprox(Vector3d(0, 0, r), 1e-6) &&
                        tb.isApprox(Vector3d(r, 0, 0), 1e-6);
    CHECK((taFloor || taWall));
  }
  CHECK(validCount >= 2);
}

TEST_CASE("floor/wall: the chamfer section sets back t along each wall")
{
  // Same inner 90-degree corner as the tangency-frame case. A chamfer of t cuts
  // the corner off at t from the crease measured along each wall, so the two
  // setback points are the vertex offset by t in x (floor) and t in z (wall) —
  // note this is a distance along the surface, not the inscribed radius a fillet
  // of t would give.
  const double t = 3.0;
  const auto model = box(10.0, 10.0, 1.0) + box(1.0, 10.0, 10.0);
  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);

  const auto edges = selectedEdges(mm, adj, 45.0, /*wantConcave=*/true);
  REQUIRE(edges.size() == 1);
  const auto chains = buildChains(mm, edges);
  REQUIRE(chains.size() == 1);

  const auto sections = wedgeSections(mm, adj, chains[0], t, /*concave=*/true);
  REQUIRE(sections.size() >= 2);

  int validCount = 0;
  for (const auto& w : sections) {
    if (!w.valid) continue;
    ++validCount;

    // p[3] is the crease vertex nudged into the material; recover the vertex as
    // the pentagon's untouched corners minus their setback.
    const Vector3d TA = w.p[0];
    const Vector3d TB = w.p[1];
    const Vector3d v(1.0, TA.y(), 1.0);

    const Vector3d a = TA - v;
    const Vector3d b = TB - v;
    const bool aFloor =
      a.isApprox(Vector3d(t, 0, 0), 1e-6) && b.isApprox(Vector3d(0, 0, t), 1e-6);
    const bool aWall =
      a.isApprox(Vector3d(0, 0, t), 1e-6) && b.isApprox(Vector3d(t, 0, 0), 1e-6);
    CHECK((aFloor || aWall));

    // The primed corners sit just inside the material: below the floor plane
    // z=1 for the floor-side point, and inside the wall (x<1) for the other.
    const double eps = 1e-3 * t;
    const Vector3d TAo = w.p[4];
    const Vector3d TBo = w.p[2];
    const Vector3d floorSide = aFloor ? TAo : TBo;
    const Vector3d wallSide = aFloor ? TBo : TAo;
    CHECK(floorSide.z() == Approx(1.0 - eps).margin(1e-9));
    CHECK(wallSide.x() == Approx(1.0 - eps).margin(1e-9));
  }
  CHECK(validCount >= 2);
}

TEST_CASE("floor/wall: the chamfer wedge is the expected prism")
{
  // The concave crease runs the full 10 in y, so the wedge is a triangular prism
  // of cross-section t^2/2 and length 10 — plus the sliver the eps overshoot
  // adds past each wall, which is why this is a loose bound rather than equality.
  const double t = 3.0;
  const auto model = box(10.0, 10.0, 1.0) + box(1.0, 10.0, 10.0);
  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/true));

  const auto wedge = buildWedgeSolid(mm, adj, chains, t, /*concave=*/true);
  REQUIRE_FALSE(wedge.IsEmpty());

  const double nominal = 0.5 * t * t * 10.0;
  CHECK(wedge.Volume() == Approx(nominal).epsilon(0.01));

  // It fills the crease: unioning it with the model must not enlarge the
  // bounding box, i.e. the tool stays inside the corner rather than sticking out.
  const auto box0 = model.BoundingBox();
  const auto box1 = (model + wedge).BoundingBox();
  for (int i = 0; i < 3; ++i) {
    CHECK(box1.min[i] == Approx(box0.min[i]).margin(1e-9));
    CHECK(box1.max[i] == Approx(box0.max[i]).margin(1e-9));
  }
}

TEST_CASE("cube: the bevel wedge cuts all twelve edges and only those")
{
  // A bevel is the concave construction with both signs flipped, so the same
  // machinery must remove material instead of adding it. Twelve identical prisms
  // of cross-section t^2/2 and length s, less the corner overlaps.
  const double s = 10.0, t = 1.0;
  const auto cube = box(s, s, s);
  const MergedMesh mm = mergeMesh(cube.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);

  const auto edges = selectedEdges(mm, adj, 45.0, /*wantConcave=*/false);
  REQUIRE(edges.size() == 12);
  const auto chains = buildChains(mm, edges);
  REQUIRE(chains.size() == 12);

  const auto wedge = buildWedgeSolid(mm, adj, chains, t, /*concave=*/false);
  REQUIRE_FALSE(wedge.IsEmpty());

  // Twelve prisms overlap in pairs at each of the eight corners, so the union is
  // below the naive sum but above the sum less one full prism per corner.
  const double prisms = 12.0 * 0.5 * t * t * s;
  CHECK(wedge.Volume() < prisms);
  CHECK(wedge.Volume() > prisms - 8.0 * 0.5 * t * t * t * 3.0);

  // Subtracting it leaves the bounding box alone — the tool bevels the edges
  // without eating a face. It removes slightly less than its own volume, since
  // the part that overshoots each wall was never inside the cube to begin with.
  const auto beveled = cube - wedge;
  const double removed = cube.Volume() - beveled.Volume();
  CHECK(removed < wedge.Volume());
  CHECK(removed == Approx(wedge.Volume()).epsilon(0.01));
  const auto box0 = cube.BoundingBox();
  const auto box1 = beveled.BoundingBox();
  for (int i = 0; i < 3; ++i) {
    CHECK(box1.min[i] == Approx(box0.min[i]).margin(1e-9));
    CHECK(box1.max[i] == Approx(box0.max[i]).margin(1e-9));
  }
}

TEST_CASE("cylinder rim: a closed chain wraps into a ring of wedge cells")
{
  // The bevel on a cylinder rim is the closed-chain case: the last station pairs
  // back to the first, so the ring must come out as one connected solid rather
  // than a ring with a gap where the chain was cut open.
  const int fn = 32;
  const double r = 10.0, h = 6.0, t = 1.0;
  const auto cyl = manifold::Manifold::Cylinder(h, r, r, fn, false);
  const MergedMesh mm = mergeMesh(cyl.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);

  const auto chains = buildChains(mm, selectedEdges(mm, adj, 60.0, /*wantConcave=*/false));
  REQUIRE(chains.size() == 2);

  const auto wedge = buildWedgeSolid(mm, adj, chains, t, /*concave=*/false);
  REQUIRE_FALSE(wedge.IsEmpty());
  // Two separate rims, and each rim closed rather than split at the seam: two
  // components, each a torus-like ring (genus 1) rather than an open arc.
  const auto parts = wedge.Decompose();
  CHECK(parts.size() == 2);
  for (const auto& part : parts) CHECK(part.Genus() == 1);

  // Each rim removes roughly the t^2/2 triangle swept round the circumference;
  // the polygonal approximation makes this a few percent light.
  const double nominal = 2.0 * 0.5 * t * t * 2.0 * M_PI * r;
  CHECK(wedge.Volume() == Approx(nominal).epsilon(0.05));
}

TEST_CASE("threshold: derived from the tessellation parameters, not hardcoded")
{
  // $fa bounds any seam OpenSCAD generates on its own, and $fn imposes 360/$fn
  // when it is set, so the coarsest seam in scope is the larger of the two. The
  // threshold is half again that, which is what keeps a seam from being mistaken
  // for a crease while leaving real features clear of it.
  CHECK(discretizer(0).getMaxSeamAngle() == Approx(12.0));    // $fa default
  CHECK(discretizer(16).getMaxSeamAngle() == Approx(22.5));   // $fn coarser than $fa
  CHECK(discretizer(64).getMaxSeamAngle() == Approx(12.0));   // $fn finer; $fa wins
  CHECK(discretizer(0, 30.0).getMaxSeamAngle() == Approx(30.0));
  CHECK(derivedThreshold(discretizer(16)) == Approx(33.75));
}

TEST_CASE("threshold: cylinder side seams are rejected at every tessellation")
{
  // The same claim as the stability case above, but through the threshold the
  // operator actually uses rather than a hand-picked 60 degrees. A cylinder's
  // side seams are exactly 360/$fn, which the derived threshold clears by half
  // again at every resolution, while the 90-degree rims never come close to it.
  // This is the case a hardcoded constant gets wrong: at $fn=8 the seams are 45
  // degrees and any fixed threshold below 67.5 would fillet them.
  for (const int fn : {8, 16, 64}) {
    CAPTURE(fn);
    const auto cyl = manifold::Manifold::Cylinder(1.0, 1.0, 1.0, fn, false);
    const ClassCounts c = classify(cyl, derivedThreshold(discretizer(fn)));

    CHECK(c.feature == static_cast<size_t>(2 * fn));
    CHECK(c.featureConvex == static_cast<size_t>(2 * fn));
  }
}

TEST_CASE("threshold: a real crease shallower than the caller's facets is dropped")
{
  // The two tests above check the direction that keeps a cylinder smooth: a seam
  // must never be read as a crease. This is the other direction, which nothing
  // covered — a crease the SHAPE really has, shallower than the tessellation the
  // caller happens to be working at, is silently not a feature.
  //
  // The gable's apex turns 30 degrees and its shoulders 75. At $fa = 12 the
  // threshold is 18 and both are features; at $fn = 8 it is 67.5, and the apex
  // drops out while the shoulders stay. Nothing is said about it. Fifteen
  // feature edges become fourteen, and which fourteen depends on a variable the
  // caller set to control smoothness.
  const auto roof = roofPrism();
  const Vector3d apexA(30.0, 18.0, 0.0), apexB(30.0, 18.0, 40.0);
  const Vector3d shoulderA(0.0, 10.0, 0.0), shoulderB(0.0, 10.0, 40.0);

  // Five vertical edges plus both five-edge rims, so long as the apex counts.
  CHECK(classify(roof, derivedThreshold(discretizer(0))).feature == 15);     // $fa = 12 -> 18
  CHECK(classify(roof, derivedThreshold(discretizer(24))).feature == 15);    // -> 22.5
  CHECK(classify(roof, derivedThreshold(discretizer(8))).feature == 14);     // -> 67.5

  CHECK(isSelected(roof, derivedThreshold(discretizer(0)), apexA, apexB));
  CHECK_FALSE(isSelected(roof, derivedThreshold(discretizer(8)), apexA, apexB));
  // And it is only the shallow one that goes: the shoulders clear 67.5, so the
  // same call rounds one crease of this roof and not the other.
  CHECK(isSelected(roof, derivedThreshold(discretizer(8)), shoulderA, shoulderB));

  // min_angle= is the way out, and it has to be, because the threshold cannot
  // both keep a smooth cylinder smooth and pick up a crease shallower than that
  // cylinder's own facets.
  CHECK(isSelected(roof, 20.0, apexA, apexB));
}

TEST_CASE("threshold: a facet angle equal to the threshold is rejected, all of it")
{
  // A model tessellated at one setting and filleted at another, which is what a
  // $fn inside a module and a $fn at the call site give you. The caller is at
  // $fn = 24 throughout, so the threshold is 22.5 and only the model moves.
  //
  // The tie is reachable rather than hypothetical: the threshold is half again
  // the caller's facet angle, so a model at two thirds the caller's $fn turns by
  // exactly it. Either side of that the answer was always clean — at $fn = 12
  // the facets turn 30 and every vertical seam is a crease, at $fn = 32 they
  // turn 11.25 and none is. At $fn = 16 they turn 22.5, and a bare comparison
  // took twelve of the sixteen: the dihedral of a tessellated cylinder does not
  // come out equal at every seam in double precision, so four landed a few ulp
  // low. Nothing in the model distinguishes those four.
  //
  // isFeatureAngle settles it by rejecting the tie, so a prism stays a prism
  // rather than having three quarters of its facets rounded.
  const double threshold = derivedThreshold(discretizer(24));
  CHECK(threshold == Approx(22.5));

  const auto rims = [](int fn) { return static_cast<size_t>(2 * fn); };
  CHECK(classify(manifold::Manifold::Cylinder(20.0, 10.0, 10.0, 12, false), threshold).feature ==
        rims(12) + 12);   // facets at 30 degrees: all twelve seams are creases
  CHECK(classify(manifold::Manifold::Cylinder(20.0, 10.0, 10.0, 32, false), threshold).feature ==
        rims(32));        // facets at 11.25: none of them is
  CHECK(classify(manifold::Manifold::Cylinder(20.0, 10.0, 10.0, 16, false), threshold).feature ==
        rims(16));        // facets at 22.5 exactly: the rims only, all sixteen alike
}

TEST_CASE("threshold: the tie margin is far below any angle a caller would choose")
{
  // The margin exists to absorb the last few ulp of an exact tie and must not
  // reach anything else. A hair under the threshold was already rejected and
  // still is; a hair over it is now rejected too, and that hair is 2e-8 degrees
  // at this threshold — six orders below the smallest angle anyone writes.
  const double t = 22.5;
  CHECK_FALSE(isFeatureAngle(std::nextafter(t, 0.0), t));
  CHECK_FALSE(isFeatureAngle(t, t));
  CHECK_FALSE(isFeatureAngle(std::nextafter(t, 90.0), t));
  CHECK(isFeatureAngle(t + 1e-6, t));
  CHECK(isFeatureAngle(22.500001, t));

  // And it is relative, so it behaves the same wherever the threshold sits.
  CHECK_FALSE(isFeatureAngle(67.5, 67.5));
  CHECK(isFeatureAngle(67.5 + 1e-6, 67.5));
  CHECK_FALSE(isFeatureAngle(1e-3, 1e-3));
  CHECK(isFeatureAngle(1e-3 + 1e-9, 1e-3));
}

TEST_CASE("threshold: a solid with no crease in it selects nothing and builds nothing")
{
  // The empty tool is correct here and is byte-for-byte what an operator that
  // did nothing at all would return, so emptiness alone proves nothing. What the
  // operator owes is that nothing was SELECTED, not that nothing happened to be
  // built: a sphere has 768 two-face edges at this tessellation and not one of
  // them is a crease.
  const auto sphere = manifold::Manifold::Sphere(10.0, 32);
  const ClassCounts c = classify(sphere, derivedThreshold(discretizer(32)));
  CHECK(c.twoFace > 700);
  CHECK(c.feature == 0);
  CHECK(c.nonManifold == 0);

  const MergedMesh mm = mergeMesh(sphere.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, derivedThreshold(discretizer(32)),
                                                    /*wantConcave=*/false));
  CHECK(chains.empty());
  CHECK(buildRoundSolid(mm, adj, chains, 1.0, /*concave=*/false, 24).IsEmpty());
}

TEST_CASE("refillet: a rounded solid re-read carries creases its shape does not have")
{
  // Rounding a model that already carries a round is an ordinary thing to write,
  // and the question it was queued to answer was whether the arc facets fall
  // under the threshold. They do — that part is uninteresting. What the model
  // comes back with instead is hundreds of creases of BOTH signs, on a shape
  // that is convex everywhere and should have none.
  //
  // The shape is not what is wrong. The exact rounded cube, built as the hull of
  // eight spheres, classifies at zero features, and the tool's own result is
  // within a fraction of a percent of its volume. What is wrong is the mesh: the
  // beads meet the flat faces and each other tangentially, and a tangential
  // meeting triangulates into slivers whose normals are numerical noise. On a
  // 40 mm part the creases below sit on edges three orders of magnitude smaller.
  //
  // Those slivers are the same ones that make a filleted pocket impossible to
  // dilate through CGAL, measured from the other end: there they stop a check
  // running, here they make the operator's own classifier disagree with the
  // shape it just built. Reducing the tangential contact is what fixes both.
  const double r = 5.0, side = 40.0;
  const auto model = box(side, side, side);
  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 18.0, /*wantConcave=*/false));
  const auto rounded = model - buildRoundSolid(mm, adj, chains, r, /*concave=*/false, 24);
  REQUIRE_FALSE(rounded.IsEmpty());
  CHECK(rounded.Genus() == 0);

  // The exact answer: a cube of side-2r grown by a ball of r.
  const double a = side - 2 * r;
  const double exactVolume = a * a * a + 6 * a * a * r + 3 * M_PI * a * r * r +
                             4 * M_PI * r * r * r / 3.0;
  CHECK(rounded.Volume() == Approx(exactVolume).epsilon(0.01));

  std::vector<manifold::Manifold> balls;
  for (const double x : {r, side - r})
    for (const double y : {r, side - r})
      for (const double z : {r, side - r})
        balls.push_back(manifold::Manifold::Sphere(r, 48).Translate(manifold::vec3(x, y, z)));
  CHECK(classify(manifold::Manifold::Hull(balls), 18.0).feature == 0);

  // And the tool's, which is the finding. Bounds rather than exact counts: the
  // numbers move with the arc tessellation, the fact does not.
  const ClassCounts c = classify(rounded, 18.0);
  CHECK(c.feature > 100);
  CHECK(c.featureConcave > 100);   // on a solid that is convex everywhere

  // How thin the mesh gets where it does this. A bound and not a number: what it
  // is for is to say that a crease the classifier reports here sits on an edge no
  // real feature of a 40 mm part would, so the disagreement is the triangulation
  // and not the shape. It has moved twice as the tangential contact was reduced —
  // 6.6e-4, then 1.3e-3 when the arcs were taken past their walls, then 5.6e-3
  // when the corner cells stopped being hulled against the beads they close — so
  // it is written where the measurement is rather than where it once was.
  const MergedMesh rm = mergeMesh(rounded.GetMeshGL64());
  const auto radj = buildEdgeAdjacency(rm.tris);
  double shortest = std::numeric_limits<double>::max();
  for (const auto& key : selectedEdges(rm, radj, 18.0, /*wantConcave=*/true))
    shortest = std::min(shortest, (rm.pos[key.first] - rm.pos[key.second]).norm());
  CHECK(shortest < 5e-3 * r);
}

TEST_CASE("chains: two seams that cross do so where neither is still a crease")
{
  // Two perpendicular cylinders of EQUAL radius are the one arrangement whose
  // seam loops cross rather than sit apart. x^2+y^2 = x^2+z^2 gives y = +-z, so
  // the seam is two ellipses meeting at (+-R, 0, z). Make the radii unequal and
  // the algebra gives z^2 = y^2 - (Rr^2 - Rb^2), which has no solution near
  // y = 0: the loops separate and never meet at all. There is no transversal
  // version of this crossing — equality is what creates it.
  //
  // And equality is also what destroys it. Where the two ellipses meet, the two
  // cylinders share a tangent plane, so the dihedral of the seam runs to zero on
  // the way in. It is under the threshold long before it arrives, and the crease
  // is cut there like any other shallow feature. What comes back is four open
  // arcs, not two crossing loops, and the crossings are not junctions because by
  // the time the spine reaches them there is no spine.
  //
  // So the valence-four junction on a curved crease that this shape was expected
  // to provide does not exist in it, and cannot be recovered by tessellating
  // finer: tangency is the reason the loops cross. A curved junction has to come
  // from creases that meet at an angle — two bosses overlapping on a plate, say
  // — and not from this one.
  const double RR = 10.0, HR = 60.0, ZB = 30.0, LB = 30.0;
  const auto tee = [&](double rb) {
    return manifold::Manifold::Cylinder(HR, RR, RR, 48, false) +
           manifold::Manifold::Cylinder(2 * LB, rb, rb, 48, false)
             .Translate(manifold::vec3(0.0, 0.0, -LB))
             .Rotate(-90, 0, 0)
             .Translate(manifold::vec3(0.0, 0.0, ZB));
  };
  const double threshold = derivedThreshold(discretizer(48));

  const MergedMesh equal = mergeMesh(tee(RR).GetMeshGL64());
  const auto equalAdj = buildEdgeAdjacency(equal.tris);
  const auto equalChains =
    buildChains(equal, selectedEdges(equal, equalAdj, threshold, /*wantConcave=*/true));
  CHECK(equalChains.size() == 4);
  for (const auto& ch : equalChains) CHECK_FALSE(ch.closed);

  // The stretch that went missing is the tangential one. Every concave edge
  // within 3 mm of the crossing plane turns by less than the threshold.
  for (const auto& kv : equalAdj) {
    if (kv.second.size() != 2) continue;
    const auto ec = classifyEdge(equal, kv.first, equal.tris[kv.second[0]],
                                 equal.tris[kv.second[1]]);
    if (!ec.concave) continue;
    const Vector3d mid = 0.5 * (equal.pos[kv.first.first] + equal.pos[kv.first.second]);
    if (std::abs(mid.y()) < 3.0) CHECK(ec.dihedralDeg < threshold);
  }

  // A branch one millimetre narrower does not cross the run's seam at all: two
  // closed loops, each entirely clear of the plane the crossings would be in.
  const MergedMesh apart = mergeMesh(tee(RR - 1.0).GetMeshGL64());
  const auto apartAdj = buildEdgeAdjacency(apart.tris);
  const auto apartChains =
    buildChains(apart, selectedEdges(apart, apartAdj, threshold, /*wantConcave=*/true));
  REQUIRE(apartChains.size() == 2);
  for (const auto& ch : apartChains) {
    CHECK(ch.closed);
    double nearest = std::numeric_limits<double>::max();
    for (const int v : ch.verts) nearest = std::min(nearest, std::abs(apart.pos[v].y()));
    CHECK(nearest > 4.0);   // sqrt(Rr^2 - Rb^2) = 4.36, and the loops start there
  }
}

TEST_CASE("chains: two curved creases and a straight one meet at a real junction")
{
  // Two overlapping bosses on a plate. Each base ring is a closed curved
  // concave crease, the two rings cross at two points, and where the cylinders
  // interpenetrate a straight crease runs up from each crossing. Unlike the
  // equal-radius tee above, the creases here meet at an ANGLE — the bosses
  // overlap by 6 of their 20 diameter, so the groove walls turn about 91
  // degrees and no pair of surfaces is tangent anywhere near the crossings.
  // That is what makes this the suite's one junction on a curved spine.
  //
  // The rings do not survive as rings. Each is cut at both crossings, leaving
  // one open arc per boss running from crossing to crossing round the outside
  // of its own cylinder, so four open chains come back: two curved, two
  // straight. The crossings then carry three chain ends each, which is a
  // junction by definition, and the corner solve pins exactly one seated ball
  // at each — three walls, one corner, the same answer a cube's vertex gives.
  const double RB = 10.0, HB = 20.0, PT = 6.0;
  const double xa = 22.0, xb = 36.0, yc = 20.0;
  const auto boss = [&](double x) {
    return manifold::Manifold::Cylinder(HB, RB, RB, 24, false)
      .Translate(manifold::vec3(x, yc, PT));
  };
  const manifold::Manifold model = box(60.0, 40.0, PT) + boss(xa) + boss(xb);

  const double threshold = derivedThreshold(discretizer(24));
  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, threshold, /*wantConcave=*/true));

  REQUIRE(chains.size() == 4);
  size_t straight = 0;
  for (const auto& ch : chains) {
    CHECK_FALSE(ch.closed);
    // Both ends of every chain sit on the crossing line x = (xa + xb) / 2. The
    // grooves run up it; the arcs run from one end of it to the other.
    CHECK(mm.pos[ch.verts.front()].x() == Approx(0.5 * (xa + xb)));
    CHECK(mm.pos[ch.verts.back()].x() == Approx(0.5 * (xa + xb)));
    if (ch.verts.size() == 2) ++straight;
  }
  CHECK(straight == 2);   // the two grooves, one segment each

  // Both crossings are junctions, at every size the case is drawn at, and both
  // are solved rather than run out to the sharp vertex.
  for (const double r : {1.0, 2.0, 4.0}) {
    const auto junctions = chainJunctions(mm, adj, chains, r, /*concave=*/true);
    REQUIRE(junctions.size() == 2);
    for (const auto& j : junctions) {
      CHECK(j.faceNormals.size() == 3);
      CHECK(j.ballCentres.size() == 1);
      CHECK(mm.pos[j.vert].x() == Approx(0.5 * (xa + xb)));
      CHECK(mm.pos[j.vert].z() == Approx(PT));
    }
  }
}

TEST_CASE("non-manifold input: shared edges are counted and nothing crashes")
{
  // Two cubes meeting at one edge, and two meeting at one vertex. Neither is a
  // solid, and OpenSCAD will hand the operator either one without complaint, so
  // what matters is that the operator reaches a verdict rather than a signal.
  //
  // The edge-sharing pair has one edge with four incident triangles, which the
  // classifier counts as non-manifold and declines to classify — it is not a
  // two-face edge, so it can be neither concave nor convex and never reaches the
  // selection. The vertex-sharing pair is subtler and comes out clean: sharing a
  // point makes no edge non-manifold, so every edge is an ordinary two-face one
  // and both cubes are rounded independently. Only the genus, -1 for two
  // components, says anything happened.
  const auto cube = box(10.0, 10.0, 10.0);

  const auto sharedEdge = cube + cube.Translate(manifold::vec3(10.0, 10.0, 0.0));
  const ClassCounts e = classify(sharedEdge, 18.0);
  CHECK(e.nonManifold == 1);
  CHECK(e.feature == 22);          // 24 cube edges less the two the shared one replaces

  const auto sharedVertex = cube + cube.Translate(manifold::vec3(10.0, 10.0, 10.0));
  const ClassCounts v = classify(sharedVertex, 18.0);
  CHECK(v.nonManifold == 0);
  CHECK(v.feature == 24);
  CHECK(sharedVertex.Genus() == -1);

  // And the tool builds on both without dying: the shared edge is simply absent
  // from the chains, so its two cubes are rounded as if they never touched.
  for (const auto& model : {sharedEdge, sharedVertex}) {
    const MergedMesh mm = mergeMesh(model.GetMeshGL64());
    const auto adj = buildEdgeAdjacency(mm.tris);
    const auto chains = buildChains(mm, selectedEdges(mm, adj, 18.0, /*wantConcave=*/false));
    CHECK_FALSE(buildRoundSolid(mm, adj, chains, 1.0, /*concave=*/false, 24).IsEmpty());
  }
}

TEST_CASE("wedge: a non-positive setback builds nothing")
{
  // The size reaches here straight from the script, so zero and negative are
  // ordinary user input, not internal errors. Both must come back empty rather
  // than as an inside-out or zero-volume solid that a later boolean would carry.
  const auto model = box(10.0, 10.0, 1.0) + box(1.0, 10.0, 10.0);
  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/true));
  REQUIRE(chains.size() == 1);

  CHECK(buildWedgeSolid(mm, adj, chains, 0.0, /*concave=*/true).IsEmpty());
  CHECK(buildWedgeSolid(mm, adj, chains, -1.0, /*concave=*/true).IsEmpty());
}

TEST_CASE("wedge: an empty chain list builds nothing")
{
  // What every unselected model reaches: a sphere-like solid with no crease
  // anywhere, or a tool whose sign matches none of the edges present.
  const auto cube = box(10.0, 10.0, 10.0);
  const MergedMesh mm = mergeMesh(cube.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);

  // A cube has no concave edge at all, so the concave tool selects nothing.
  const auto edges = selectedEdges(mm, adj, 45.0, /*wantConcave=*/true);
  CHECK(edges.empty());
  const auto chains = buildChains(mm, edges);
  CHECK(chains.empty());
  CHECK(buildWedgeSolid(mm, adj, chains, 1.0, /*concave=*/true).IsEmpty());
}

namespace {

// The floor-and-wall L whose single concave crease runs the length of y: the
// simplest model with one open chain of exactly one segment, so a brush cutting
// it lands at a parameter that can be read off by hand.
manifold::Manifold floorAndWall() { return box(10.0, 10.0, 1.0) + box(1.0, 10.0, 10.0); }

// What buildFilletTool does with the node's brush children: drop the chains the
// brush misses, and give each of the rest the stretches of itself it covers.
std::vector<Chain> brushed(const MergedMesh& mm, const std::vector<Chain>& chains,
                           const manifold::Manifold& brush, double size)
{
  const BrushVolume volume(brush.GetMeshGL64());
  std::vector<Chain> out;
  for (Chain chain : chains) {
    const size_t n = chain.verts.size();
    const size_t segments = n < 2 ? 0 : (chain.closed ? n : n - 1);
    auto keep = chainSelection(mm, chain, volume, 0.01 * size);
    if (keep.empty()) continue;
    if (keep.size() == 1 && keep.front().first <= 0.0 &&
        keep.front().second >= static_cast<double>(segments))
      keep.clear();
    chain.keep = std::move(keep);
    out.push_back(std::move(chain));
  }
  return out;
}

}  // namespace

TEST_CASE("brush: a point is placed by the face it would leave through")
{
  // Not by counting crossings: the nearest face a ray meets says which side the
  // ray started on, and one bad crossing further out cannot flip the answer.
  const BrushVolume volume(box(2.0, 2.0, 2.0).GetMeshGL64());
  CHECK(volume.contains(Vector3d(1.0, 1.0, 1.0)));
  CHECK(volume.contains(Vector3d(0.01, 1.9, 1.0)));
  CHECK_FALSE(volume.contains(Vector3d(3.0, 1.0, 1.0)));
  CHECK_FALSE(volume.contains(Vector3d(-0.01, 1.0, 1.0)));

  // A brush with no geometry at all contains nothing; the caller must not read
  // that as "everything".
  const BrushVolume nothing{manifold::MeshGL64{}};
  CHECK(nothing.empty());
  CHECK_FALSE(nothing.contains(Vector3d::Zero()));
}

TEST_CASE("brush: the spine is cut where the brush crosses it, not at a station")
{
  // The crease runs y = 0 .. 10 with a station only at each end. A brush ending
  // at y = 4 has to come back as the parameter 0.4 — a fixed physical point —
  // rather than as a decision about which of the two stations is in.
  const auto model = floorAndWall();
  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/true));
  REQUIRE(chains.size() == 1);
  REQUIRE(chains[0].verts.size() == 2);
  REQUIRE(mm.pos[chains[0].verts.front()].y() == Approx(0.0));

  const auto brush = box(20.0, 5.0, 20.0).Translate(manifold::vec3(-5.0, -1.0, -5.0));
  const auto keep = chainSelection(mm, chains[0], BrushVolume(brush.GetMeshGL64()), 0.01);
  REQUIRE(keep.size() == 1);
  CHECK(keep[0].first == Approx(0.0));
  CHECK(keep[0].second == Approx(0.4));

  // A brush containing the whole crease reports the whole of it, and one that
  // contains none of it reports nothing — the two answers the caller has to be
  // able to tell apart.
  const auto all = box(40.0, 40.0, 40.0).Translate(manifold::vec3(-10.0, -10.0, -10.0));
  const auto whole = chainSelection(mm, chains[0], BrushVolume(all.GetMeshGL64()), 0.01);
  REQUIRE(whole.size() == 1);
  CHECK(whole[0].first == Approx(0.0));
  CHECK(whole[0].second == Approx(1.0));

  const auto elsewhere = box(2.0, 2.0, 2.0).Translate(manifold::vec3(50.0, 50.0, 50.0));
  CHECK(chainSelection(mm, chains[0], BrushVolume(elsewhere.GetMeshGL64()), 0.01).empty());
}

namespace {

// A rectangular column, meshed the way a `translate() cube()` brush reaches the
// operator: every face split in two, and the end faces split along the diagonal
// that runs from one corner to its opposite in x and y — which is the line a
// spine down the middle of the column arrives on.
manifold::MeshGL64 column(double w, double z0, double z1)
{
  const double lo = -w / 2, hi = w / 2;
  const double xyz[8][3] = {{lo, lo, z0}, {hi, lo, z0}, {hi, hi, z0}, {lo, hi, z0},
                            {lo, lo, z1}, {hi, lo, z1}, {hi, hi, z1}, {lo, hi, z1}};
  const int tri[12][3] = {{0, 2, 1}, {0, 3, 2}, {7, 5, 6}, {5, 7, 4}, {0, 1, 5}, {0, 5, 4},
                          {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
  manifold::MeshGL64 mesh;
  mesh.numProp = 3;
  for (const auto& v : xyz)
    for (const double c : v) mesh.vertProperties.push_back(c);
  for (const auto& t : tri)
    for (const int v : t) mesh.triVerts.push_back(v);
  return mesh;
}

}  // namespace

TEST_CASE("brush: a spine down the middle of a brush face is still cut by it")
{
  // The two triangles of a quad face meet on a diagonal through its centre, and
  // a brush is drawn symmetric about the crease it selects more often than not —
  // a column straddling one edge is how a model names that edge — so the spine
  // meets the brush's end face exactly on the line between its two triangles.
  // Tested exactly, both can reject it: the barycentric coordinate that decides
  // is 1 to within an ulp and lands the wrong side on each. The crossing is then
  // lost and the brush goes on selecting the crease while no longer bounding it
  // — the bead runs to the end of the chain instead of stopping at the brush.
  //
  // Whether it is lost depends on the magnitudes in the arithmetic, so it comes
  // and goes with the width of the column: measured over these widths, every one
  // at or below 0.4 lost it and every one above kept it.
  for (const double w : {0.02, 0.2, 0.3, 0.4, 0.5, 1.0, 4.0}) {
    const BrushVolume volume(column(w, -1.0, 14.0));

    const auto out = volume.segmentCrossings(Vector3d(0.0, 0.0, 0.0), Vector3d(0.0, 0.0, 20.0));
    REQUIRE(out.size() == 1);
    CHECK(out[0].t == Approx(0.7));
    CHECK_FALSE(out[0].entering);

    // In through one end face and out through the other is one passage each and
    // not two: the pair of triangles sharing the line report the same crossing,
    // and it is collapsed rather than counted twice.
    const auto through =
      volume.segmentCrossings(Vector3d(0.0, 0.0, -6.0), Vector3d(0.0, 0.0, 14.0));
    REQUIRE(through.size() == 2);
    CHECK(through[0].entering);
    CHECK(through[0].t == Approx(0.25));
    CHECK_FALSE(through[1].entering);
    CHECK(through[1].t == Approx(1.0));
  }

  // And the same thing where it is reached from: the width decides which creases
  // are selected, never how far along one the selection runs.
  const auto model = floorAndWall();
  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/true));
  REQUIRE(chains.size() == 1);

  for (const double w : {0.02, 0.3, 4.0}) {
    // The crease runs along y at (x, z) = (1, 1); the column straddles it and
    // stops at y = 4, which is the parameter 0.4 of the one segment.
    const manifold::Manifold across =
      manifold::Manifold(column(w, -2.0, 4.0)).Rotate(-90.0, 0.0, 0.0).Translate(
        manifold::vec3(1.0, 0.0, 1.0));
    const auto keep = chainSelection(mm, chains[0], BrushVolume(across.GetMeshGL64()), 0.01);
    REQUIRE(keep.size() == 1);
    CHECK(keep[0].first == Approx(0.0));
    CHECK(keep[0].second == Approx(0.4));
  }
}

TEST_CASE("brush: a graze too short to be a bead is dropped")
{
  // A brush face nearly tangent to the spine crosses it twice a hair apart. The
  // stub of bead that would leave is never what anyone asked for, and the user
  // sees nothing built rather than a sliver they cannot find.
  const auto model = floorAndWall();
  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/true));
  REQUIRE(chains.size() == 1);

  const auto sliver = box(20.0, 0.001, 20.0).Translate(manifold::vec3(-5.0, -0.0005, -5.0));
  const BrushVolume volume(sliver.GetMeshGL64());
  CHECK(chainSelection(mm, chains[0], volume, /*minLength=*/0.005).empty());
  // The same graze is a real selection when the tool is small enough for it.
  CHECK(chainSelection(mm, chains[0], volume, /*minLength=*/1e-6).size() == 1);
}

TEST_CASE("brush: the clipped bead is the whole bead cut by the brush")
{
  // The claim the case suite draws as a picture, stated as a volume. Clipping
  // the spine and clipping the finished tool have to agree — which they only do
  // because the canal overhangs the wedge's cut: shortening it to match would
  // let its round cap bulge back through the cut plane and scoop a dish out of
  // the flat end face.
  const double r = 0.5;
  const auto model = floorAndWall();
  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/true));
  REQUIRE(chains.size() == 1);

  const auto brush = box(20.0, 5.0, 20.0).Translate(manifold::vec3(-5.0, -1.0, -5.0));
  const auto clipped =
    buildRoundSolid(mm, adj, brushed(mm, chains, brush, r), r, /*concave=*/true, 24);
  const auto expected = buildRoundSolid(mm, adj, chains, r, /*concave=*/true, 24) ^ brush;
  REQUIRE_FALSE(clipped.IsEmpty());
  REQUIRE_FALSE(expected.IsEmpty());

  // Both directions, because they fail differently: material the brush should
  // have kept is the scoop, and material past the brush is a bead that did not
  // stop.
  const double scale = expected.Volume();
  CHECK((expected - clipped).Volume() < 1e-6 * scale);
  CHECK((clipped - expected).Volume() < 1e-6 * scale);

  // And the cap is where the brush ends, not at the station beyond it.
  CHECK(clipped.BoundingBox().max[1] == Approx(4.0).margin(1e-9));
}

TEST_CASE("brush: a slanted brush face still caps square to the crease")
{
  // The brush selects a point along an edge; it does not cut geometry. So the
  // angle its own surface crosses the spine at must not reach the result: the
  // cap is the crease's cross-section, square to the spine, wherever the brush
  // face happens to lean.
  //
  // The brush here is a half-space whose plane passes through (1, 4, 1) — a
  // point ON the crease — with its normal tilted well away from the spine in
  // both of the directions available to it. Whatever the rotation does, the
  // plane still contains that point, so the crossing is still at y = 4.
  const double r = 0.5;
  const auto model = floorAndWall();
  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/true));
  REQUIRE(chains.size() == 1);

  const auto brush = box(200.0, 200.0, 200.0)
                       .Translate(manifold::vec3(-100.0, -200.0, -100.0))
                       .Rotate(20.0, 0.0, 35.0)
                       .Translate(manifold::vec3(1.0, 4.0, 1.0));

  const auto unclipped = buildRoundSolid(mm, adj, chains, r, /*concave=*/true, 24);
  const auto clipped =
    buildRoundSolid(mm, adj, brushed(mm, chains, brush, r), r, /*concave=*/true, 24);
  REQUIRE_FALSE(clipped.IsEmpty());

  // What it must equal: the bead cut by the plane PERPENDICULAR to the crease
  // at the point the brush crossed it — not by the brush.
  const auto square = box(200.0, 204.0, 200.0).Translate(manifold::vec3(-100.0, -200.0, -100.0));
  const auto expected = unclipped ^ square;
  const double scale = expected.Volume();
  CHECK((expected - clipped).Volume() < 1e-6 * scale);
  CHECK((clipped - expected).Volume() < 1e-6 * scale);

  // And it must NOT equal the bead cut by the brush, or the case proves nothing:
  // the slanted plane shaves the far side of the bead where the square one does
  // not, which is exactly the difference being ruled out.
  const auto slanted = unclipped ^ brush;
  CHECK((clipped - slanted).Volume() > 1e-3 * scale);

  // Read off the ends: the square cap stops the whole bead at 4, while the brush
  // surface runs on to 4.35 on the far side of it. If the brush's angle ever
  // leaked into the result, this is the number that would carry it.
  CHECK(clipped.BoundingBox().max[1] == Approx(4.0).margin(1e-9));
  CHECK(slanted.BoundingBox().max[1] > 4.3);
}

TEST_CASE("brush: a corner every crease still reaches keeps its corner cell")
{
  // A brush that takes the corner of a cube but only part of each edge running
  // out of it. All three creases reach the vertex, so the corner is built; the
  // beads are capped flat partway along instead of at the far corners, which are
  // not selected at all and get nothing.
  const double r = 1.0;
  const auto cube = box(10.0, 10.0, 10.0);
  const MergedMesh mm = mergeMesh(cube.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/false));
  REQUIRE(chains.size() == 12);

  const auto brush = box(5.0, 5.0, 5.0).Translate(manifold::vec3(-1.0, -1.0, -1.0));
  const auto selected = brushed(mm, chains, brush, r);
  // Only the three edges at the origin corner meet the brush.
  REQUIRE(selected.size() == 3);

  const auto junctions = chainJunctions(mm, adj, selected, r, /*concave=*/false);
  REQUIRE(junctions.size() == 1);
  CHECK(mm.pos[junctions[0].vert].isApprox(Vector3d::Zero()));
  CHECK(junctions[0].ballCentres.size() == 1);

  const auto tool = buildRoundSolid(mm, adj, selected, r, /*concave=*/false, 24);
  REQUIRE_FALSE(tool.IsEmpty());
  // The three beads stop at the brush, a little past it where the corner cell
  // reaches; nothing runs on to the far corners ten away.
  CHECK(tool.BoundingBox().max[0] < 4.5);
  CHECK(tool.BoundingBox().max[1] < 4.5);
  CHECK(tool.BoundingBox().max[2] < 4.5);

  // What matters more than the extent: the corner closes. A cube with one corner
  // rounded is still one solid with no hole in it.
  const auto rounded = cube - tool;
  REQUIRE_FALSE(rounded.IsEmpty());
  CHECK(rounded.Genus() == 0);
}

TEST_CASE("brush: a corner one crease is cut short of gets no corner cell")
{
  // The other half of the rule. The brush covers two of the three edges at the
  // corner and stops short of the third, so the corner is not built: a cell
  // closing three beads when only two arrive is a lump on the model, not a
  // corner. The two that are there are capped flat, like any other clip.
  const double r = 1.0;
  const auto cube = box(10.0, 10.0, 10.0);
  const MergedMesh mm = mergeMesh(cube.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/false));

  // Lifted off z = 0, so the two edges in that plane are missed entirely and the
  // vertical one is taken over z = 2 .. 7.
  const auto brush = box(5.0, 5.0, 5.0).Translate(manifold::vec3(-1.0, -1.0, 2.0));
  const auto selected = brushed(mm, chains, brush, r);
  REQUIRE(selected.size() == 1);
  CHECK(chainJunctions(mm, adj, selected, r, /*concave=*/false).empty());

  const auto tool = buildRoundSolid(mm, adj, selected, r, /*concave=*/false, 24);
  REQUIRE_FALSE(tool.IsEmpty());
  CHECK(tool.BoundingBox().min[2] == Approx(2.0).margin(1e-9));
  CHECK(tool.BoundingBox().max[2] == Approx(7.0).margin(1e-9));
}

TEST_CASE("brush: a selection swallowed by the junction setback builds no bead")
{
  // A brush reaching the corner by less than the setback the junction truncates
  // each spine by selects a stretch that no longer has any sections in it: what
  // it asked for is already inside the corner cell. That has to build nothing
  // along the creases, not everything — the empty selection is the map of a real
  // one, and the whole-chain reading of an empty list is for chains no brush
  // touched at all.
  const double r = 3.0;
  const auto cube = box(20.0, 20.0, 20.0);
  const MergedMesh mm = mergeMesh(cube.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/false));
  REQUIRE(chains.size() == 12);

  // Reaching 2 mm past the origin corner, against a setback of r = 3 at a
  // 90-degree crease.
  const auto brush = box(5.0, 5.0, 5.0).Translate(manifold::vec3(-3.0, -3.0, -3.0));
  const auto selected = brushed(mm, chains, brush, r);
  REQUIRE(selected.size() == 3);

  const auto tool = buildRoundSolid(mm, adj, selected, r, /*concave=*/false, 24);
  REQUIRE_FALSE(tool.IsEmpty());
  // Nothing runs the length of an edge: the tool stays inside the corner it was
  // pointed at. The whole-chain reading would take all three creases end to end.
  CHECK(tool.BoundingBox().max[0] < 5.0);
  CHECK(tool.BoundingBox().max[1] < 5.0);
  CHECK(tool.BoundingBox().max[2] < 5.0);

  // And it removes less than the same corner asked for over a stretch that does
  // survive the mapping, rather than nine times more.
  const auto wide = box(20.0, 20.0, 20.0).Translate(manifold::vec3(-3.0, -3.0, -3.0));
  const auto full = buildRoundSolid(mm, adj, brushed(mm, chains, wide, r), r,
                                    /*concave=*/false, 24);
  const double removed = (cube - (cube - tool)).Volume();
  const double removedFull = (cube - (cube - full)).Volume();
  CHECK(removed < removedFull);

  // A brush this far inside the setback no longer gets a corner cell either —
  // covering the vertex is not covering the stretch a corner occupies — so what
  // is left here is three short beads meeting unclosed. Still one solid.
  const auto rounded = cube - tool;
  REQUIRE_FALSE(rounded.IsEmpty());
  CHECK(rounded.Genus() == 0);
}

TEST_CASE("brush: a corner the brush reaches but does not cover is not built")
{
  // A corner cell is a fixed size: it is hulled from the seated ball and the
  // sections the beads stop at, and there is no perpendicular to clip it against
  // in three directions at once. So a brush that reaches the vertex by a
  // fraction of the setback and gets the whole cell is the brush contract broken
  // by the rest of it — while along an edge the same brush is honoured exactly.
  // The rule is therefore coverage of the setback, not arrival at the vertex.
  const double r = 3.0;
  const auto cube = box(20.0, 20.0, 20.0);
  const MergedMesh mm = mergeMesh(cube.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/false));

  // Two boxes on the same corner, one reaching 2 mm down each edge from it and
  // one reaching 6. Both select the same three creases.
  for (const double D : {2.0, 6.0}) {
    const auto brush =
      box(D + 1.0, D + 1.0, D + 1.0).Translate(manifold::vec3(-1.0, -1.0, 20.0 - D));
    const auto selected = brushed(mm, chains, brush, r);
    REQUIRE(selected.size() == 3);

    const auto junctions = chainJunctions(mm, adj, selected, r, /*concave=*/false);
    const auto uncovered = uncoveredCorners(mm, selected, r);
    const auto tool = buildRoundSolid(mm, adj, selected, r, /*concave=*/false, 24);
    REQUIRE_FALSE(tool.IsEmpty());

    if (D < r) {
      CHECK(junctions.empty());
      REQUIRE(uncovered.size() == 1);
      CHECK(mm.pos[uncovered[0]].isApprox(Vector3d(0.0, 0.0, 20.0)));
      // The cell alone is an order of magnitude more than this; what is left is
      // three beads two millimetres long.
      CHECK(tool.Volume() < 3.0);
    } else {
      REQUIRE(junctions.size() == 1);
      CHECK(mm.pos[junctions[0].vert].isApprox(Vector3d(0.0, 0.0, 20.0)));
      CHECK(uncovered.empty());
      CHECK(tool.Volume() > 30.0);
      // And the beads run out exactly as far as the brush does, not to the
      // setback: 6 mm down each edge from the vertex.
      CHECK(tool.BoundingBox().min[2] == Approx(20.0 - D).margin(1e-6));
    }
  }
}

namespace {

// One model put through the size gate: the chains a tool would walk on it, and
// the verdict on each.
struct SizeRun
{
  MergedMesh mm;
  std::map<EdgeKey, std::vector<int>> adj;
  std::vector<Chain> chains;
  std::vector<SizeVerdict> verdicts;
};

SizeRun sizeRun(const manifold::Manifold& solid, double size, bool concave, bool wedge = false,
                double thresholdDeg = 20.0)
{
  SizeRun run;
  run.mm = mergeMesh(solid.GetMeshGL64());
  run.adj = buildEdgeAdjacency(run.mm.tris);
  run.chains = buildChains(run.mm, selectedEdges(run.mm, run.adj, thresholdDeg, concave));
  run.verdicts =
    checkChainSizes(run.mm, run.adj, run.chains, size, concave, wedge, thresholdDeg);
  return run;
}

size_t countFault(const std::vector<SizeVerdict>& verdicts, SizeFault fault)
{
  return static_cast<size_t>(
    std::count_if(verdicts.begin(), verdicts.end(),
                  [fault](const SizeVerdict& v) { return v.fault == fault; }));
}

// An L whose two faces are `arm` long: one concave crease, and the only thing
// limiting the size is how far each face reaches.
manifold::Manifold ell(double arm, double thickness, double height)
{
  return box(arm, thickness, height) + box(thickness, arm, height);
}

}  // namespace

TEST_CASE("zzdebug rib", "[.]")
{
  const auto model = box(60.0, 40.0, 6.0) + box(30.0, 4.0, 20.0).Translate(manifold::vec3(10.0, 18.0, 6.0));
  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 20.0, true));
  const auto surf = smoothSurfaces(mm, adj, 20.0);
  for (size_t ci = 0; ci < chains.size(); ++ci) {
    const auto cs = chainContacts(mm, adj, chains[ci], 1.5, true, false, surf, 3);
    for (const auto& c : cs) {
      if (!c.valid) continue;
      double bA = 1e30, bB = 1e30;
      for (size_t t = 0; t < mm.tris.size(); ++t) {
        const auto& tr = mm.tris[t];
        if (surf[t] == c.surfaceA)
          bA = std::min(bA, pointTriangleDistance(c.TA, mm.pos[tr.v[0]], mm.pos[tr.v[1]], mm.pos[tr.v[2]]));
        if (surf[t] == c.surfaceB)
          bB = std::min(bB, pointTriangleDistance(c.TB, mm.pos[tr.v[0]], mm.pos[tr.v[1]], mm.pos[tr.v[2]]));
      }
      if (bA > 0.05 || bB > 0.05)
        WARN("chain " << ci << " v=(" << c.v.x() << "," << c.v.y() << "," << c.v.z() << ")"
             << " TA=(" << c.TA.x() << "," << c.TA.y() << "," << c.TA.z() << ") sA=" << c.surfaceA << " offA=" << bA
             << " TB=(" << c.TB.x() << "," << c.TB.y() << "," << c.TB.z() << ") sB=" << c.surfaceB << " offB=" << bB);
    }
  }
}

TEST_CASE("size: two beads sharing a face fit until their tangency lines meet")
{
  // The crowding question is about the material the blend uses, and that is the
  // corner between the two tangency lines — not the seated ball, which goes on
  // reaching a whole radius along each wall past where it touches it. Asked of
  // the ball, a cube refuses every radius past a third of its side; asked of the
  // corner, it takes every radius up to half of it, which is where the two beads
  // on a shared face actually meet. The ratio is the answer, not the size: the
  // same fraction on three cubes.
  for (const double side : {5.0, 10.0, 20.0}) {
    const auto cube = box(side, side, side);
    const SizeRun fits = sizeRun(cube, 0.4 * side, /*concave=*/false);
    REQUIRE(fits.chains.size() == 12);
    CHECK(countFault(fits.verdicts, SizeFault::Fits) == 12);

    // And past the meeting point every one of them is refused, as crowded and
    // not as running off a face: there is face left, it is spoken for.
    const SizeRun over = sizeRun(cube, 0.55 * side, /*concave=*/false);
    CHECK(countFault(over.verdicts, SizeFault::Crowded) == 12);
    // What is reported is how far off the crease the feature competing with it
    // sits — the neighbouring bead's root on the face they share.
    CHECK(over.verdicts[0].amount == Approx(0.45 * side).margin(1e-6));
  }

  // The shape the gate now allows is an ordinary one. r/L = 0.4 on a 5 mm cube
  // is a rounded cube with 1 mm of flat left on each face, and the tool builds
  // the hull of eight spheres to within a thousandth of its volume.
  const double side = 5.0, r = 2.0;
  const SizeRun run = sizeRun(box(side, side, side), r, /*concave=*/false);
  const auto rounded = box(side, side, side) -
                       buildRoundSolid(run.mm, run.adj, run.chains, r, /*concave=*/false, 64);
  REQUIRE_FALSE(rounded.IsEmpty());
  CHECK(rounded.Genus() == 0);

  std::vector<manifold::Manifold> balls;
  for (const double x : {r, side - r})
    for (const double y : {r, side - r})
      for (const double z : {r, side - r})
        balls.push_back(manifold::Manifold::Sphere(r, 64).Translate(manifold::vec3(x, y, z)));
  const auto ref = manifold::Manifold::Hull(balls);
  CHECK(rounded.Volume() == Approx(ref.Volume()).epsilon(1e-3));
  // Volume alone would pass on a shape of the right size in the wrong place.
  CHECK(((rounded - ref) + (ref - rounded)).Volume() < 0.01 * ref.Volume());
}

TEST_CASE("size: a blend wider than the face it must meet is refused")
{
  // The tangency points of a radius-6 bead land 6 along each 30 mm face; at 35
  // they land 5 past the end of both, so the bead would touch nothing. There is
  // no smaller bead that is still the one asked for, so the crease is dropped.
  const auto model = ell(40.0, 10.0, 60.0);

  const SizeRun fits = sizeRun(model, 6.0, /*concave=*/true);
  REQUIRE(fits.chains.size() == 1);
  CHECK(fits.verdicts[0].fault == SizeFault::Fits);

  const SizeRun over = sizeRun(model, 35.0, /*concave=*/true);
  REQUIRE(over.chains.size() == 1);
  CHECK(over.verdicts[0].fault == SizeFault::OffFace);
  CHECK(over.verdicts[0].amount == Approx(5.0).margin(1e-6));
  CHECK(over.verdicts[0].where.x() == Approx(10.0));
  CHECK(over.verdicts[0].where.y() == Approx(10.0));
}

TEST_CASE("size: a chamfer setback is measured against the same face")
{
  // The wedge tools take the setback directly, so the same 30 mm face is what
  // bounds it — and at 90 degrees the ball whose tangency points are those
  // setback points has exactly the setback as its radius.
  const auto model = ell(40.0, 10.0, 60.0);

  CHECK(sizeRun(model, 6.0, /*concave=*/true, /*wedge=*/true).verdicts[0].fault ==
        SizeFault::Fits);
  const SizeRun over = sizeRun(model, 35.0, /*concave=*/true, /*wedge=*/true);
  CHECK(over.verdicts[0].fault == SizeFault::OffFace);
  CHECK(over.verdicts[0].amount == Approx(5.0).margin(1e-6));
}

TEST_CASE("size: creases closer together than the tool's reach are refused")
{
  // A wall standing 10 mm from another wall's foot. At r = 3 the two beads have
  // 4 mm between them and both are built; at r = 8 each one's ball reaches
  // through the other's bead to its root, and neither crease can be filled as
  // asked. The third crease, on the far side of the second wall, has nothing
  // within reach and survives both times.
  const auto model = box(60.0, 40.0, 5.0) + box(4.0, 40.0, 25.0) +
                     box(4.0, 40.0, 25.0).Translate(manifold::vec3(14.0, 0.0, 5.0));

  const SizeRun fits = sizeRun(model, 3.0, /*concave=*/true);
  REQUIRE(fits.chains.size() == 3);
  CHECK(countFault(fits.verdicts, SizeFault::Fits) == 3);

  const SizeRun over = sizeRun(model, 8.0, /*concave=*/true);
  REQUIRE(over.chains.size() == 3);
  CHECK(countFault(over.verdicts, SizeFault::Crowded) == 2);
  CHECK(countFault(over.verdicts, SizeFault::Fits) == 1);

  // The two that fail are the pair facing each other across the gap, and the
  // distance reported is to the neighbour's bead root, not to its crease.
  for (size_t i = 0; i < over.chains.size(); ++i)
    if (over.verdicts[i].fault == SizeFault::Crowded)
      CHECK(over.verdicts[i].where.x() < 15.0);
}

TEST_CASE("size: a wall's own curvature is not an overshoot")
{
  // A dome standing on a plate: one closed concave crease, where the sphere
  // meets the top face, and room to spare on both walls. Stepping off the ball
  // centre along a wall normal to find the contact point assumes the wall flat,
  // and on a doubly curved one it lands off the surface by the sagitta, r^2/2R
  // — 0.0625 at r = 1 on this R = 8 dome, and four times that at r = 2 — which
  // is a miss of the construction, not of the model, and no finer tessellation
  // reduces it. Asking the wall for its nearest point instead puts the contact
  // on the dome, and the question becomes whether that point is inside the wall
  // or on its rim.
  const auto dome = box(40.0, 40.0, 10.0) + manifold::Manifold::Sphere(8.0, 64)
                                              .Translate(manifold::vec3(20.0, 20.0, 10.0));
  for (const double r : {0.3, 1.0, 2.0}) {
    const SizeRun run = sizeRun(dome, r, /*concave=*/true);
    REQUIRE(run.chains.size() == 1);
    CHECK(run.verdicts[0].fault == SizeFault::Fits);
  }

  // And the gate has not gone quiet on the curved wall: crowd the same crease
  // against the edge of a plate that barely holds the dome, and it is refused
  // for having run off it.
  const auto perched = box(18.0, 18.0, 10.0).Translate(manifold::vec3(11.0, 11.0, 0.0)) +
                       manifold::Manifold::Sphere(8.0, 64).Translate(manifold::vec3(20.0, 20.0, 10.0));
  const SizeRun over = sizeRun(perched, 2.0, /*concave=*/true);
  REQUIRE(over.chains.size() == 1);
  CHECK(over.verdicts[0].fault == SizeFault::OffFace);

  // What eventually stops the dome is the plate and not the dome, which is
  // worth pinning because the opposite was expected: a blend whose radius
  // approaches the curvature it follows was supposed to run out of solutions.
  // It does not. The crease is a circle in the plate's own plane, and the ball
  // seated in it meets the plate one radius outside that circle, so the only
  // question is whether the plate reaches — the same question a flat wall asks.
  // The first plate above is 20 from the crease to its edge, so a radius well
  // over the dome's own 8 still fits, and what refuses the larger one is how
  // far the plate reaches rather than anything about the sphere.
  CHECK(sizeRun(dome, 10.0, /*concave=*/true).verdicts[0].fault == SizeFault::Fits);
  const SizeRun wide = sizeRun(dome, 14.0, /*concave=*/true);
  const SizeRun wider = sizeRun(dome, 18.0, /*concave=*/true);
  CHECK(wide.verdicts[0].fault == SizeFault::OffFace);
  CHECK(wider.verdicts[0].fault == SizeFault::OffFace);
  CHECK(wider.verdicts[0].amount > wide.verdicts[0].amount);
}

TEST_CASE("size: only the stretch of crease the brushes kept is asked about")
{
  // The spike's slant creases run out of room for a radius of 1 above z = 80,
  // and are refused whole. A brush that picks out the lower half picks out the
  // half that has the room, so there is nothing left to refuse — the bead the
  // user asked for is buildable, and warning about the part of the crease they
  // did not select would be refusing work nobody asked for.
  const auto needle = manifold::Manifold::Cylinder(120.0, 3.0, 0.0, 3, false);
  const MergedMesh mm = mergeMesh(needle.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 20.0, /*wantConcave=*/false));
  REQUIRE(chains.size() == 6);

  const auto whole = checkChainSizes(mm, adj, chains, 1.0, /*concave=*/false, false, 20.0);
  CHECK(countFault(whole, SizeFault::OffFace) == 3);

  const auto low = box(20.0, 20.0, 60.0).Translate(manifold::vec3(-10.0, -10.0, -10.0));
  const auto lowChains = brushed(mm, chains, low, 1.0);
  REQUIRE(lowChains.size() == 6);
  CHECK(countFault(checkChainSizes(mm, adj, lowChains, 1.0, false, false, 20.0),
                   SizeFault::Fits) == 6);

  // The other half is still refused, and the sample it is refused at is one the
  // brush kept rather than wherever on the crease the room first ran out.
  const auto high = box(20.0, 20.0, 40.0).Translate(manifold::vec3(-10.0, -10.0, 90.0));
  const auto highChains = brushed(mm, chains, high, 1.0);
  const auto verdicts = checkChainSizes(mm, adj, highChains, 1.0, false, false, 20.0);
  CHECK(countFault(verdicts, SizeFault::OffFace) == highChains.size());
  for (const auto& v : verdicts) CHECK(v.where.z() > 90.0);
}

TEST_CASE("size: a cube's rounds are limited by the edge across the face")
{
  // Nothing is off its face here — r = 30 reaches back only 30 of the 40 mm
  // available — but the edge on the other side of that face reaches 30 the
  // other way, so the two want the same material. Every edge fails for that
  // reason, and none for the first.
  const auto model = box(40.0, 40.0, 60.0);

  const SizeRun fits = sizeRun(model, 6.0, /*concave=*/false);
  REQUIRE(fits.chains.size() == 12);
  CHECK(countFault(fits.verdicts, SizeFault::Fits) == 12);

  const SizeRun over = sizeRun(model, 30.0, /*concave=*/false);
  CHECK(countFault(over.verdicts, SizeFault::Crowded) == 12);
  CHECK(countFault(over.verdicts, SizeFault::OffFace) == 0);
}

TEST_CASE("runout: a corner with no seated ball fades the blend out to the vertex")
{
  // A 3 x 120 spike. The ball seated against all three slant walls sits eighty
  // radii down the axis, which the sanity bound refuses, so the apex gets no
  // corner. Truncating into it anyway is what used to leave the beads stopping
  // 80 mm short with a sharp spike standing over them; the ramp takes the radius
  // to nothing at the vertex instead, so the tool reaches the tip.
  const double r = 1.0;
  const auto needle = manifold::Manifold::Cylinder(120.0, 3.0, 0.0, 3, false);
  const MergedMesh mm = mergeMesh(needle.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 20.0, /*wantConcave=*/false));

  const auto junctions = chainJunctions(mm, adj, chains, r, /*concave=*/false);
  const Junction *apex = nullptr;
  for (const auto& j : junctions)
    if (mm.pos[j.vert].z() > 119.0) apex = &j;
  REQUIRE(apex != nullptr);
  CHECK(apex->ballCentres.empty());

  const auto tool = buildRoundSolid(mm, adj, chains, r, /*concave=*/false, 24);
  REQUIRE_FALSE(tool.IsEmpty());
  CHECK(tool.BoundingBox().max[2] == Approx(120.0).margin(1e-6));

  // And what it leaves behind is still a solid: the point of the fallback is a
  // valid shape whose blend fades, not a hole where a corner should be.
  const auto rounded = needle - tool;
  REQUIRE_FALSE(rounded.IsEmpty());
  CHECK(rounded.Genus() == 0);
}

TEST_CASE("size: a feature that tapers below the tool's reach is refused")
{
  // The same spike, asked the question the operator asks before building
  // anything. A crease has stations only where the mesh has vertices, and this
  // one has two: the base corner and the apex, both junctions. Everything that
  // is wrong with a radius of 1 here happens between them — the spike is
  // narrower than the tool's own setback for its last 27 mm, so the tool would
  // take the tip off rather than round it — and it is caught only because the
  // crease is sampled along its length and not just at its ends.
  const auto needle = manifold::Manifold::Cylinder(120.0, 3.0, 0.0, 3, false);
  const MergedMesh mm = mergeMesh(needle.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 20.0, /*wantConcave=*/false));

  const auto verdicts =
    checkChainSizes(mm, adj, chains, 1.0, /*concave=*/false, /*wedge=*/false, 20.0);

  // Six creases: three up the slant, three around the base. The slant ones are
  // refused and the base ones are not — the spike is only too thin where it has
  // tapered, and the base is as wide as it ever gets.
  REQUIRE(chains.size() == 6);
  CHECK(countFault(verdicts, SizeFault::OffFace) == 3);
  CHECK(countFault(verdicts, SizeFault::Fits) == 3);
  for (const auto& v : verdicts)
    if (v.fault == SizeFault::OffFace) CHECK(v.where.z() > 80.0);
}

TEST_CASE("surfaces: a seam joins two triangles into one wall, a crease does not")
{
  // What the size gate asks a contact point to land on is a surface, not a
  // triangle: a bore's facets are one wall the ball rides, while a cube's six
  // faces stay six.
  const auto cube = box(10.0, 10.0, 10.0);
  const MergedMesh cm = mergeMesh(cube.GetMeshGL64());
  const auto cAdj = buildEdgeAdjacency(cm.tris);
  const auto cSurfaces = smoothSurfaces(cm, cAdj, 20.0);
  REQUIRE(cSurfaces.size() == cm.tris.size());
  CHECK(std::set<int>(cSurfaces.begin(), cSurfaces.end()).size() == 6);

  // A 32-sided cylinder: the side is one surface at 11.25 degrees a seam, and
  // the two caps are one each.
  const auto cyl = manifold::Manifold::Cylinder(10.0, 5.0, 5.0, 32, false);
  const MergedMesh ym = mergeMesh(cyl.GetMeshGL64());
  const auto yAdj = buildEdgeAdjacency(ym.tris);
  const auto ySurfaces = smoothSurfaces(ym, yAdj, 20.0);
  CHECK(std::set<int>(ySurfaces.begin(), ySurfaces.end()).size() == 3);
}

TEST_CASE("junctions: a cube corner solves to the one ball seated in all three walls")
{
  // Eight corners, each three walls, each one place a ball of r can sit: the
  // symmetric answer is r in from all three faces, which for a cube of side s
  // puts every centre on a corner of the box [r, s-r]^3.
  const double s = 16.0, r = 3.0;
  const auto model = box(s, s, s);
  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/false));

  const auto junctions = chainJunctions(mm, adj, chains, r, /*concave=*/false);
  REQUIRE(junctions.size() == 8);
  for (const auto& j : junctions) {
    CHECK(j.faceNormals.size() == 3);
    REQUIRE(j.ballCentres.size() == 1);
    const Vector3d v = mm.pos[j.vert];
    const Vector3d P = j.ballCentres.front();
    for (int k = 0; k < 3; ++k) CHECK(P[k] == Approx(v[k] == 0.0 ? r : s - r));
  }
}

TEST_CASE("junctions: four walls meeting on an axis keep the one centre they share")
{
  // A four-sided pyramid's apex. Four walls do not generally leave the ball one
  // place to sit, so the corner is solved as every triple of walls filtered down
  // to the ones clear of the rest — and here all four triples give the same
  // answer, on the axis. The filter must keep it rather than reject it for
  // sitting exactly on the fourth wall, and the dedup must not report it four
  // times.
  const double R = 30.0, h = 60.0, r = 6.0;
  const auto model = manifold::Manifold::Cylinder(h, R, 0.0, 4, false);
  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/false));

  const auto junctions = chainJunctions(mm, adj, chains, r, /*concave=*/false);
  // The apex plus the four base corners.
  REQUIRE(junctions.size() == 5);

  const auto apex = std::find_if(junctions.begin(), junctions.end(), [&](const Junction& j) {
    return mm.pos[j.vert].z() == Approx(h);
  });
  REQUIRE(apex != junctions.end());
  CHECK(apex->faceNormals.size() == 4);
  REQUIRE(apex->ballCentres.size() == 1);

  const Vector3d P = apex->ballCentres.front();
  CHECK(P.x() == Approx(0.0).margin(1e-9));
  CHECK(P.y() == Approx(0.0).margin(1e-9));
  // Seated against all four slant walls, so it is exactly r from each of them.
  for (const Vector3d& n : apex->faceNormals)
    CHECK(-n.dot(P - mm.pos[apex->vert]) == Approx(r));
}

TEST_CASE("junctions: walls that cannot pin a point down produce no corner")
{
  // Three creases meeting where the walls are all parallel to one axis: the 3x3
  // is singular, and the solve would hand a hull coordinates at 1e30 rather than
  // fail cleanly. A prism whose cross-section is an L has exactly that shape at
  // its concave crease — every wall is vertical — and the crease is a plain
  // two-face edge, so no junction should be reported at all.
  const auto model = box(10.0, 10.0, 4.0) + box(4.0, 20.0, 4.0);
  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/true));

  for (const auto& j : chainJunctions(mm, adj, chains, 1.0, /*concave=*/true))
    for (const Vector3d& P : j.ballCentres) {
      CHECK(std::isfinite(P.norm()));
      CHECK((P - mm.pos[j.vert]).norm() <= 10.0);
    }
}

TEST_CASE("spine: a slit too narrow to roll a ball into is skipped, not solved")
{
  // A V-groove cut into a block: its two walls meet at the apex line with a
  // dihedral of `openingDeg`, so the outward normals are 180 - openingDeg apart.
  // As that approaches 180 the bisector degenerates and the ball centre
  // d = r/cos(phi/2) runs to infinity — coordinates Manifold would either throw
  // on or spend unbounded memory hulling. The frame has to come back invalid
  // instead, and the wedge with it.
  auto slitBlock = [](double openingDeg) {
    const double halfWidth = 6.0 * std::tan(openingDeg * 0.5 * M_PI / 180.0);
    std::vector<manifold::vec3> prism;
    for (const double y : {-1.0, 11.0}) {
      prism.emplace_back(5.0, y, 5.0);                 // apex line
      prism.emplace_back(5.0 - halfWidth, y, 11.0);    // mouth, above the block
      prism.emplace_back(5.0 + halfWidth, y, 11.0);
    }
    return box(10.0, 10.0, 10.0) - manifold::Manifold::Hull(prism);
  };

  auto framesAtApex = [](const manifold::Manifold& model, double r) {
    const MergedMesh mm = mergeMesh(model.GetMeshGL64());
    const auto adj = buildEdgeAdjacency(mm.tris);
    const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/true));
    std::vector<SpineFrame> all;
    for (const auto& chain : chains) {
      const auto f = spineFrames(mm, adj, chain, r, /*concave=*/true);
      all.insert(all.end(), f.begin(), f.end());
    }
    return all;
  };

  SECTION("a 60-degree groove is ordinary geometry")
  {
    const auto frames = framesAtApex(slitBlock(60.0), 1.0);
    REQUIRE_FALSE(frames.empty());
    int valid = 0;
    for (const auto& f : frames) {
      if (!f.valid) continue;
      ++valid;
      CHECK(f.phiDeg == Approx(120.0).margin(0.5));
      // d = r/cos(phi/2) = 2r at 120 degrees, and every coordinate finite.
      CHECK((f.C - f.v).norm() == Approx(2.0).margin(0.05));
      CHECK(std::isfinite(f.C.x()));
    }
    CHECK(valid >= 2);
  }

  SECTION("a half-degree slit is refused")
  {
    const auto frames = framesAtApex(slitBlock(0.5), 1.0);
    REQUIRE_FALSE(frames.empty());
    for (const auto& f : frames) {
      CHECK_FALSE(f.valid);
      // Refused for the reason claimed — the walls really are 179.5 degrees
      // apart — rather than for some earlier failure that happens to look alike.
      CHECK(f.phiDeg == Approx(179.5).margin(0.5));
    }
  }
}

TEST_CASE("debug markers: one colour class per kind of edge present")
{
  // The overlay's whole job is to say which class an edge landed in, so the
  // assertion that matters is how many distinct colours come out — a marker in
  // the wrong colour is a classification bug wearing a disguise, and the count
  // of colours is what an image would be read for anyway.
  const double threshold = 45.0;

  SECTION("cube: convex only")
  {
    const auto cube = box(10.0, 10.0, 10.0);
    const MergedMesh mm = mergeMesh(cube.GetMeshGL64());
    const auto ps = debugEdgeMarkers(mm, buildEdgeAdjacency(mm.tris), threshold);
    REQUIRE(ps != nullptr);
    // Only the twelve convex edges are drawn: the six face diagonals are
    // coplanar and skipped, so there is no seam colour and no concave colour.
    CHECK(ps->colors.size() == 1);
  }

  SECTION("L-shape: concave and convex")
  {
    const auto ell = box(20.0, 6.0, 10.0) + box(6.0, 20.0, 10.0);
    const MergedMesh mm = mergeMesh(ell.GetMeshGL64());
    const auto ps = debugEdgeMarkers(mm, buildEdgeAdjacency(mm.tris), threshold);
    REQUIRE(ps != nullptr);
    CHECK(ps->colors.size() == 2);
  }

  SECTION("cylinder: convex rims plus rejected side seams")
  {
    const auto cyl = manifold::Manifold::Cylinder(10.0, 6.0, 6.0, 16, false);
    const MergedMesh mm = mergeMesh(cyl.GetMeshGL64());
    const auto ps = debugEdgeMarkers(mm, buildEdgeAdjacency(mm.tris), threshold);
    REQUIRE(ps != nullptr);
    // 22.5-degree side seams fall below the threshold and are drawn as seams,
    // the rims as convex features — two classes, both present.
    CHECK(ps->colors.size() == 2);
  }
}

TEST_CASE("debug markers: nothing to draw returns nothing")
{
  // The caller appends whatever comes back into one PolySet, so "no geometry"
  // has to be a null return rather than an empty solid it would have to detect.
  const MergedMesh empty;
  CHECK(debugEdgeMarkers(empty, {}, 45.0) == nullptr);
  CHECK(debugSpineMarkers(empty, {}) == nullptr);

  const auto cube = box(10.0, 10.0, 10.0);
  const MergedMesh mm = mergeMesh(cube.GetMeshGL64());
  CHECK(debugSpineMarkers(mm, {}) == nullptr);
}

TEST_CASE("debug markers: the spine overlay draws every part of the frame")
{
  // Ball centre, both tangency points and the legs between them are four
  // separate colours; if one is missing the overlay looks plausible and hides
  // exactly the quantity being debugged.
  const double r = 3.0;
  const auto model = box(10.0, 10.0, 1.0) + box(1.0, 10.0, 10.0);
  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/true));
  REQUIRE(chains.size() == 1);

  const auto frames = spineFrames(mm, adj, chains[0], r, /*concave=*/true);
  const auto ps = debugSpineMarkers(mm, frames);
  REQUIRE(ps != nullptr);
  CHECK(ps->colors.size() == 4);
}

#endif  // ENABLE_MANIFOLD
