// Unit tests for the fillet edge-classification internals: MeshGL vertex
// merging, edge -> two-face adjacency, and concave/convex classification. These
// exercise the pure combinatorics directly on hand-built Manifold primitives,
// where the correct answer is a count or a boolean — the cases a rendered image
// reports poorly (a concavity sign flip is invisible until it becomes a gouge; a
// seam that leaks past the angle filter is one stray edge among hundreds).

#include <catch2/catch_all.hpp>

#ifdef ENABLE_MANIFOLD

#include "geometry/fillet/FilletBuilder_internal.h"

#include <cmath>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
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

  const auto frames = spineFrames(mm, adj, chains[0], r);
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
      const auto f = spineFrames(mm, adj, chain, r);
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

  const auto frames = spineFrames(mm, adj, chains[0], r);
  const auto ps = debugSpineMarkers(mm, frames);
  REQUIRE(ps != nullptr);
  CHECK(ps->colors.size() == 4);
}

#endif  // ENABLE_MANIFOLD
