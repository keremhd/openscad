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

// An explicit threshold at a known angle, the value a min_angle= would carry.
// Tests that need *some* threshold to drive the builder at use this so the
// number in the test is the number the builder sees; it is not the rule the
// operator applies on its own, which is kDefaultCreaseThresholdDeg.
double derivedThreshold(const CurveDiscretizer& d) { return 1.5 * d.getMaxSeamAngle(); }

// Classify at the threshold the operator applies with no min_angle= named.
ClassCounts classifyAuto(const manifold::Manifold& m)
{
  return classify(m, kDefaultCreaseThresholdDeg);
}

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
    CHECK(ch.stationCount() == fn);
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
  CHECK(chains[0].stationCount() == 2);
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

  const auto sections = wedgeSections(mm, adj, chains[0], t, /*concave=*/true, 45.0);
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

  const auto wedge = buildWedgeSolid(mm, adj, chains, t, /*concave=*/true, 45.0);
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

  const auto wedge = buildWedgeSolid(mm, adj, chains, t, /*concave=*/false, 45.0);
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

  const auto wedge = buildWedgeSolid(mm, adj, chains, t, /*concave=*/false, 60.0);
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

// A branch pipe standing on a run pipe: the crease is a closed space curve, and
// the wall on the run's side curves *across* it, which is the shape a fixed
// overshoot cannot stand past. The two cases below both use it.
manifold::Manifold pipeTee(int fn = 32)
{
  return manifold::Manifold::Cylinder(60.0, 10.0, 10.0, fn, false) +
         manifold::Manifold::Cylinder(25.0, 6.0, 6.0, fn, false)
           .Rotate(-90, 0, 0)
           .Translate(manifold::vec3(0.0, 0.0, 30.0));
}

TEST_CASE("curved wall: the overshoot is measured against the wall, not fixed")
{
  // Every point of the pentagon that stands past a wall is stepped off the
  // crease along that wall's normal, which is a plane tangent to the wall at the
  // station. A flat wall stays in that plane and the fixed hair — 1e-3 of the
  // size, pinned by the floor/wall case above — clears it. The run pipe here
  // falls away from it by r_setback^2 / 2R, which at a 2 mm setback on a radius
  // 10 pipe is 0.2: a hundred times the hair, and what the wall face has to
  // stand past to be inside the material at all.
  const double t = 2.0;
  const double threshold = derivedThreshold(discretizer(32));
  const MergedMesh mm = mergeMesh(pipeTee().GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, threshold, /*wantConcave=*/true));
  REQUIRE(chains.size() == 1);

  const auto sections = wedgeSections(mm, adj, chains[0], t, /*concave=*/true, threshold);
  double most = 0.0, least = std::numeric_limits<double>::max();
  for (const auto& w : sections) {
    if (!w.valid) continue;
    for (const auto& [face, primed] : {std::pair{w.p[0], w.p[4]}, std::pair{w.p[1], w.p[2]}}) {
      most = std::max(most, (face - primed).norm());
      least = std::min(least, (face - primed).norm());
    }
  }
  // Never less than the hair, and on this shape several times the sagitta of the
  // run's own facets — the wall the setback lands on is a chord of the pipe, not
  // of one facet of it.
  CHECK(least >= Approx(1e-3 * t));
  CHECK(most > 0.15);
}

TEST_CASE("curved wall: the blend leaves no crease the model does not have")
{
  // What the overshoot standing short of a curved wall leaves behind: a ledge
  // one overshoot deep along the whole tangency line, with the tool's own faces
  // either side of it, which reads as a crease of the shape. Measured on this
  // tee before the overshoot followed the wall, the union carried 204 convex
  // feature edges turning more than 150 degrees — folds, faces turned back on
  // each other — against the model's own 96, all of them square rims.
  const double r = 2.0;
  const double threshold = derivedThreshold(discretizer(32));
  const manifold::Manifold model = pipeTee();
  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, threshold, /*wantConcave=*/true));
  const auto tool = buildRoundSolid(mm, adj, chains, r, /*concave=*/true, 32, threshold, {});
  REQUIRE_FALSE(tool.IsEmpty());

  const manifold::Manifold blended = model + tool;
  CHECK(blended.Genus() == 0);

  const MergedMesh bm = mergeMesh(blended.GetMeshGL64());
  const auto badj = buildEdgeAdjacency(bm.tris);
  double sharpest = 0.0;
  for (const auto& [key, tris] : badj) {
    if (tris.size() != 2) continue;
    const EdgeClass ec = classifyEdge(bm, key, bm.tris[tris[0]], bm.tris[tris[1]]);
    if (!isFeatureAngle(ec.dihedralDeg, threshold) || ec.concave) continue;
    sharpest = std::max(sharpest, ec.dihedralDeg);
  }
  // The model's rims are square and there is nothing else: the bead meets both
  // pipes tangentially, so every convex edge left is one the user wrote.
  CHECK(sharpest == Approx(90.0).margin(0.5));
}

TEST_CASE("threshold: the default is a constant, not read from anything")
{
  // The default threshold is the same number for every solid: no render
  // variable, no property of the mesh, no facet count. A cylinder classifies to
  // its rims alone at every tessellation whose facets fall under it, and the
  // count owed is the same count at each.
  for (const int fn : {16, 32, 64}) {
    CAPTURE(fn);
    const auto cyl = manifold::Manifold::Cylinder(1.0, 1.0, 1.0, fn, false);
    CHECK(classifyAuto(cyl).feature == static_cast<size_t>(2 * fn));
  }
  // Where the constant sits, stated as the behaviour it buys: strictly between
  // the facet angle of a 7-gon and that of an 8-gon, so every prism from $fn=8
  // up keeps its walls, and none of their facet angles lands on the threshold
  // where the tie margin would decide the answer.
  CHECK(kDefaultCreaseThresholdDeg > 360.0 / 8);
  CHECK(kDefaultCreaseThresholdDeg < 360.0 / 7);
}

TEST_CASE("threshold: an untessellated solid classifies its own corners")
{
  // A cube's only non-flat edges are its twelve 90-degree corners, which clear
  // the default threshold. Nothing on the solid is read to reach that: a cube
  // and a cylinder are classified at the same number.
  const auto cube = manifold::Manifold::Cube(manifold::vec3(1.0), false);

  const ClassCounts c = classifyAuto(cube);
  CHECK(c.feature == 12);
  CHECK(c.featureConvex == 12);
}

TEST_CASE("threshold: rotating a solid by one facet does not move its classification")
{
  // The self-proving invariant. An n-gon prism turned about its own axis by
  // 360/n is the identical point set, so a tee built on a run pipe turned by one
  // facet is the identical solid — but Manifold cuts the branch's intersection
  // curve against different facets, so the mesh is not the same mesh. A
  // threshold that is a property of the shape cannot move; one that keys off
  // anything in the triangulation will.
  const int fn = 32;   // pipeTee's own facet count
  const auto branch = manifold::Manifold::Cylinder(25.0, 6.0, 6.0, fn, false)
                        .Rotate(-90, 0, 0)
                        .Translate(manifold::vec3(0.0, 0.0, 30.0));
  const auto run = manifold::Manifold::Cylinder(60.0, 10.0, 10.0, fn, false);

  const auto tee = run + branch;
  const auto turned = run.Rotate(0.0, 0.0, 360.0 / fn) + branch;
  REQUIRE(turned.Volume() == Approx(tee.Volume()).epsilon(1e-9));

  // Not vacuous: the tee carries features, and its two meshes are not the same
  // triangulation.
  CHECK(classifyAuto(tee).feature > 0);
  CHECK(classifyAuto(turned).feature == classifyAuto(tee).feature);
}

TEST_CASE("threshold: cylinder side seams are rejected at every tessellation")
{
  // A cylinder's side seams turn 360/$fn and its rims turn 90, so a threshold
  // between the two takes the rims alone. $fn=8 is the coarsest tessellation the
  // default threshold still calls flat: its seams turn 45, under the constant,
  // and an octagonal prism keeps its eight walls. A threshold below 45 rounds
  // every facet of one; at $fn=7 the seams turn 51.43 and are rounded.
  for (const int fn : {8, 16, 64}) {
    CAPTURE(fn);
    const auto cyl = manifold::Manifold::Cylinder(1.0, 1.0, 1.0, fn, false);
    const ClassCounts c = classifyAuto(cyl);

    CHECK(c.feature == static_cast<size_t>(2 * fn));
    CHECK(c.featureConvex == static_cast<size_t>(2 * fn));
  }
}

TEST_CASE("threshold: a real crease shallower than an explicit min_angle is dropped")
{
  // The tests above check the direction that keeps a cylinder smooth: a seam
  // must never be read as a crease. This is the other direction — a crease the
  // SHAPE really has, shallower than the min_angle= the caller named, is
  // silently not a feature, and nothing is said about it.
  //
  // The gable's apex turns 30 degrees and its shoulders 75. At 18 or 22.5 both
  // are features; at 67.5 the apex drops out while the shoulders stay. Fifteen
  // feature edges become fourteen.
  const auto roof = roofPrism();
  const Vector3d apexA(30.0, 18.0, 0.0), apexB(30.0, 18.0, 40.0);
  const Vector3d shoulderA(0.0, 10.0, 0.0), shoulderB(0.0, 10.0, 40.0);

  // Five vertical edges plus both five-edge rims, so long as the apex counts.
  CHECK(classify(roof, 18.0).feature == 15);
  CHECK(classify(roof, 22.5).feature == 15);
  CHECK(classify(roof, 67.5).feature == 14);

  // The same drop happens under the default threshold with no min_angle= named
  // at all: the apex turns 30, below the constant, and the shoulders turn 75.
  // A real crease shallower than the constant is the documented case for
  // min_angle=.
  CHECK(classifyAuto(roof).feature == 14);
  CHECK(classify(roof, 20.0).feature == 15);

  CHECK(isSelected(roof, 18.0, apexA, apexB));
  CHECK_FALSE(isSelected(roof, 67.5, apexA, apexB));
  // And it is only the shallow one that goes: the shoulders clear 67.5, so the
  // same call rounds one crease of this roof and not the other.
  CHECK(isSelected(roof, 67.5, shoulderA, shoulderB));
  CHECK(isSelected(roof, 20.0, apexA, apexB));
}

TEST_CASE("threshold: a facet angle equal to the threshold is rejected, all of it")
{
  // A model whose facets turn by exactly the min_angle= the caller named. The
  // threshold is 22.5 throughout and only the model moves.
  //
  // The tie is reachable rather than hypothetical: 22.5 is what a caller writes
  // for a 16-facet model. Either side of it the answer was always clean — at $fn = 12
  // the facets turn 30 and every vertical seam is a crease, at $fn = 32 they
  // turn 11.25 and none is. At $fn = 16 they turn 22.5, and a bare comparison
  // took twelve of the sixteen: the dihedral of a tessellated cylinder does not
  // come out equal at every seam in double precision, so four landed a few ulp
  // low. Nothing in the model distinguishes those four.
  //
  // isFeatureAngle settles it by rejecting the tie, so a prism stays a prism
  // rather than having three quarters of its facets rounded.
  const double threshold = 22.5;

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
  CHECK(buildRoundSolid(mm, adj, chains, 1.0, /*concave=*/false, 24,
                        derivedThreshold(discretizer(32)), {})
          .IsEmpty());
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
  const auto rounded = model - buildRoundSolid(mm, adj, chains, r, /*concave=*/false, 24, 18.0, {});
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
    // The crease as the mesh has it. A station is a mesh vertex only where the
    // walk put one there, and a resampled ring's are mostly -1.
    for (const int v : ch.rawRun()) nearest = std::min(nearest, std::abs(apart.pos[v].y()));
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
    // Every chain here is open, which is what having two end vertices means.
    int endFront = -1, endBack = -1;
    REQUIRE(ch.openEnds(endFront, endBack));
    // Both ends of every chain sit on the crossing line x = (xa + xb) / 2. The
    // grooves run up it; the arcs run from one end of it to the other.
    CHECK(mm.pos[endFront].x() == Approx(0.5 * (xa + xb)));
    CHECK(mm.pos[endBack].x() == Approx(0.5 * (xa + xb)));
    if (ch.stationCount() == 2) ++straight;
  }
  CHECK(straight == 2);   // the two grooves, one segment each

  // Both crossings are junctions, at every size the case is drawn at, and both
  // are solved rather than run out to the sharp vertex.
  for (const double r : {1.0, 2.0, 4.0}) {
    const auto junctions = chainJunctions(mm, adj, chains, r, /*concave=*/true, {});
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
    CHECK_FALSE(buildRoundSolid(mm, adj, chains, 1.0, /*concave=*/false, 24, 18.0, {}).IsEmpty());
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

  CHECK(buildWedgeSolid(mm, adj, chains, 0.0, /*concave=*/true, 45.0).IsEmpty());
  CHECK(buildWedgeSolid(mm, adj, chains, -1.0, /*concave=*/true, 45.0).IsEmpty());
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
  CHECK(buildWedgeSolid(mm, adj, chains, 1.0, /*concave=*/true, 45.0).IsEmpty());
}

namespace {

// The floor-and-wall L whose single concave crease runs the length of y: the
// simplest model with one open chain of exactly one segment, so a brush cutting
// it lands at a parameter that can be read off by hand.
manifold::Manifold floorAndWall() { return box(10.0, 10.0, 1.0) + box(1.0, 10.0, 10.0); }

// An L whose two faces are `arm` long: one concave crease, and the only thing
// limiting the size is how far each face reaches.
manifold::Manifold ell(double arm, double thickness, double height)
{
  return box(arm, thickness, height) + box(thickness, arm, height);
}

// What buildFilletTool does with the node's brush children, in two halves. The
// selection the node makes before it asks about corners: every chain the brush
// covers any part of, carrying the stretches of itself it covers.
std::vector<Chain> selection(const MergedMesh& mm, const std::vector<Chain>& chains,
                             const manifold::Manifold& brush, double size)
{
  const BrushVolume volume(brush.GetMeshGL64());
  std::vector<Chain> out;
  for (Chain chain : chains) {
    const size_t n = static_cast<size_t>(chain.stationCount());
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

// And the whole of what the node does with a brush: the selection above, with
// the stretches that arrive at a corner without covering it dropped.
std::vector<Chain> brushed(const MergedMesh& mm, const std::vector<Chain>& chains,
                           const manifold::Manifold& brush, double size,
                           std::set<int> *noCorner = nullptr)
{
  std::vector<Chain> out = selection(mm, chains, brush, size);
  dropUncoveredCorners(mm, out, chains, size, noCorner);
  return out;
}

// How many edges a selection takes, the way the echo line counts them: an edge
// any part of a kept interval overlaps.
size_t takenEdges(const std::vector<Chain>& chains)
{
  size_t taken = 0;
  for (const Chain& chain : chains) {
    const size_t n = static_cast<size_t>(chain.stationCount());
    const size_t segments = n < 2 ? 0 : (chain.closed ? n : n - 1);
    if (chain.keep.empty()) {
      taken += segments;
      continue;
    }
    for (size_t i = 0; i < segments; ++i)
      for (const SpineInterval& iv : chain.keep)
        if (std::min(iv.second, static_cast<double>(i) + 1.0) -
              std::max(iv.first, static_cast<double>(i)) >
            1e-12) {
          ++taken;
          break;
        }
  }
  return taken;
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
  REQUIRE(chains[0].stationCount() == 2);
  // Open, and its front end is the vertex at y = 0 — one assertion, since the
  // end vertex is only there to be read once the chain has said it has ends.
  int endFront = -1, endBack = -1;
  REQUIRE((chains[0].openEnds(endFront, endBack) && mm.pos[endFront].y() == Approx(0.0)));

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
  // and goes with the width of the column, and not in any order: tested exactly,
  // 0.25 and 0.35 keep the crossing while 0.2, 0.3, 0.4 and 0.45 lose it. There
  // is no width to look for, which is why the fix is in the intersection and not
  // a threshold — the widths below are a spread, not a boundary.
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
  //
  // One cut end is enough to ask the question. The graze here happens to sit on
  // the crease's own start vertex, so the stub it leaves runs from there — and it
  // is still an artefact of a brush face crossing the spine twice, not a crease
  // the brush took whole.
  const auto model = floorAndWall();
  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/true));
  REQUIRE(chains.size() == 1);

  const auto sliver = box(20.0, 0.001, 20.0).Translate(manifold::vec3(-5.0, -0.0005, -5.0));
  const BrushVolume volume(sliver.GetMeshGL64());
  CHECK(chainSelection(mm, chains[0], volume, /*debounce=*/0.005).empty());
  // The same graze is a real selection when the tool is small enough for it.
  CHECK(chainSelection(mm, chains[0], volume, /*debounce=*/1e-6).size() == 1);
}

TEST_CASE("brush: a crease selected end to end is kept however short it is")
{
  // The debounce is about what a brush cut, so it has nothing to say about a
  // crease the brush did not cut at all. A brush that contains the whole model
  // has to be a no-op, and it stops being one the moment the crease it contains
  // is shorter than a hundredth of the size — which needs the crease's length to
  // be independent of the room the blend has, and on a long thin L it is: the
  // arms give the blend all the room it needs while the crease is only as long as
  // the plate is thick.
  const double r = 400.0;
  const auto part = ell(1000.0, 5.0, 2.0);  // one concave crease, 2 long
  const MergedMesh mm = mergeMesh(part.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/true));
  REQUIRE(chains.size() == 1);

  // A brush swallowing the model whole, against a debounce of 0.01 * 400 = 4.
  const auto everything =
    box(18000.0, 18000.0, 18000.0).Translate(manifold::vec3(-9000.0, -9000.0, -9000.0));
  const auto selected = brushed(mm, chains, everything, r);
  REQUIRE(selected.size() == 1);
  // Covered end to end, which is carried as no intervals at all: the same state
  // the unbrushed path is in, and the reason the two build the same thing.
  CHECK(selected[0].keep.empty());

  const auto bare = buildRoundSolid(mm, adj, chains, r, /*concave=*/true, 24, 45.0, {});
  const auto brushedTool = buildRoundSolid(mm, adj, selected, r, /*concave=*/true, 24, 45.0, {});
  REQUIRE_FALSE(bare.IsEmpty());
  CHECK(brushedTool.Volume() == Approx(bare.Volume()));
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
    buildRoundSolid(mm, adj, brushed(mm, chains, brush, r), r, /*concave=*/true, 24, 45.0, {});
  const auto expected = buildRoundSolid(mm, adj, chains, r, /*concave=*/true, 24, 45.0, {}) ^ brush;
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

  const auto unclipped = buildRoundSolid(mm, adj, chains, r, /*concave=*/true, 24, 45.0, {});
  const auto clipped =
    buildRoundSolid(mm, adj, brushed(mm, chains, brush, r), r, /*concave=*/true, 24, 45.0, {});
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

  const auto junctions = chainJunctions(mm, adj, selected, r, /*concave=*/false, {});
  REQUIRE(junctions.size() == 1);
  CHECK(mm.pos[junctions[0].vert].isApprox(Vector3d::Zero()));
  CHECK(junctions[0].ballCentres.size() == 1);

  const auto tool = buildRoundSolid(mm, adj, selected, r, /*concave=*/false, 24, 45.0, {});
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
  // The other half of the rule, and the two-of-three case its own comment used to
  // claim while testing one. The brush takes 5 mm of each of the two edges in the
  // z = 0 plane and only h of the vertical one, so below h = r the corner is not
  // built: the cell is the full size of the seated ball whatever is selected, and
  // one built here is material the brush never asked for, in an amount that does
  // not move with h - the same lump for a brush that reached a fifth of the way
  // to the radius as for one that reached nine tenths. The stretch that fell
  // short goes with the corner it cannot have, so what is left is two beads
  // capped flat, like any other clip.
  //
  // r rather than the setback throughout, because r is what the code measures:
  // every caller of endAnchored passes the tool's own size, and the trigonometric
  // setback r * tan(phi/2) appears only in the cross-section. The two coincide at
  // 90 degrees, which is the only angle a cube offers.
  //
  // Two anchored ends arrive at that vertex, which is also what a crease refused
  // for size leaves behind, and nothing in the chains that are left tells the two
  // apart. So the volume below is a guard on more than the brush: a corner cell
  // built from the count of ends alone shows up here as a third more material.
  // Under the seam rule, which is what decides the shape here: the vertical crease
  // the brush left short is a crease of the model that no bead covers, so the two
  // beads that do arrive meet each other along it instead of a ball being built
  // over the top of it.

  const double r = 1.0;
  const auto cube = box(10.0, 10.0, 10.0);
  const MergedMesh mm = mergeMesh(cube.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/false));

  auto toolFor = [&](double h, size_t& chainsLeft, size_t& junctions) {
    const auto brush = box(6.0, 6.0, h + 1.0).Translate(manifold::vec3(-1.0, -1.0, -1.0));
    std::set<int> noCorner;
    const auto selected = brushed(mm, chains, brush, r, &noCorner);
    chainsLeft = selected.size();
    junctions = chainJunctions(mm, adj, selected, r, /*concave=*/false, noCorner).size();
    return buildRoundSolid(mm, adj, selected, r, /*concave=*/false, 24, 45.0, noCorner);
  };

  // Down to well below `0.01 * r`, where the brush pass debounces the vertical
  // stretch out of the selection altogether rather than leaving it short. Both
  // are the same answer, and a rule keyed on the arms that survived gave the
  // corner cell to the smaller brush.
  double shortVolume = 0.0;
  for (const double h : {0.005, 0.009, 0.011, 0.2, 0.5, 0.9, 0.999}) {
    size_t chainsLeft = 0, junctions = 0;
    const auto tool = toolFor(h, chainsLeft, junctions);
    CAPTURE(h);
    // Two chains are left, and neither of them is the vertical one.
    CHECK(chainsLeft == 2);
    CHECK(junctions == 0);
    REQUIRE_FALSE(tool.IsEmpty());
    // Nothing runs up the vertical edge: the two beads stand one radius off the
    // z = 0 plane and that is the whole of the tool's height.
    CHECK(tool.BoundingBox().max[2] < 1.001 * r);
    if (shortVolume == 0.0) shortVolume = tool.Volume();
    else CHECK(tool.Volume() == Approx(shortVolume));
  }
  // The whole of what the brush asked for, and the overrun that lets the two beads
  // meet. The quarter-round cross-section is r*r - pi*r*r/4, so for r = 1:
  //
  //   two beads, 5 mm each   2 * 5   * (1 - pi/4) = 2.146018
  //   two overruns, 0.1 r    2 * 0.1 * (1 - pi/4) = 0.042920
  //                                                 --------
  //                                                 2.188938
  //
  // against 2.189273 measured, 0.015% apart. The overrun is outside the cube, so
  // it is not material the caller's difference takes: measured against a hand-built
  // model of the delivered shape - two beads capped flat at 5 mm, the vertical edge
  // square - this build is 0.0006 mm3 proud.
  //
  // This pinned 1.6632 before, which is 7.75 mm of bead where the brush asked for
  // 10: the beads were being clipped back to 3.875 mm each. Against that same hand
  // model the old shape is 0.47 mm3 proud. The old value pinned the clipped bead.
  CHECK(shortVolume == Approx(2.18927).margin(1e-3));

  // Past r the same brush gets the corner, and the third bead with it.
  size_t chainsLeft = 0, junctions = 0;
  const auto tool = toolFor(1.2, chainsLeft, junctions);
  CHECK(chainsLeft == 3);
  CHECK(junctions == 1);
  CHECK(tool.Volume() > shortVolume);
}

TEST_CASE("brush: a slab over the top face rounds its edges and leaves the corners square")
{
  // The documented way to round a top surface and nothing else: brush a slab
  // less than r tall over the top face. The four top edges are covered end to
  // end, so they carry full beads; each vertical edge is caught only over the
  // slab's height, which is short of r, so it is not anchored at the corner it
  // runs into and goes with the corner it cannot have. What comes back is the
  // hull of four vertical cylinders - surface rounding - and not the hull of
  // eight spheres, which is what a corner cell at each of the four top vertices
  // would give. The two are different shapes and the user picks between them
  // with the depth of the brush, so the depth has to decide it.
  //
  // Slab heights spanning four orders of magnitude, since what matters is only
  // that they are short of r: the answer must not move with how far short. The
  // small ones are the ones that matter. Below `0.01 * size` the brush pass
  // debounces a stretch away entirely, so the vertical arms leave the selection
  // rather than staying in it uncovered — and a rule that counted the arms which
  // survived saw two where three arrived, built the corner, and turned this into
  // the eight-sphere shape. Less brush, more material, with the switch at a
  // hundredth of the radius.
  // Under the seam rule: each vertical edge is a crease of the model that no bead
  // covers, so the top beads meet along it rather than rounding its start away.

  const double r = 2.0;
  const auto cube = box(10.0, 10.0, 10.0);
  const MergedMesh mm = mergeMesh(cube.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/false));
  REQUIRE(chains.size() == 12);

  double first = 0.0;
  for (const double slab : {0.001, 0.01, 0.019, 0.021, 0.5, 1.0, 1.5, 1.999}) {
    const auto brush =
      box(12.0, 12.0, slab + 1.0).Translate(manifold::vec3(-1.0, -1.0, 10.0 - slab));
    std::set<int> noCorner;
    const auto selected = brushed(mm, chains, brush, r, &noCorner);
    CAPTURE(slab);
    // The four top edges, and only those: the vertical stubs are dropped with
    // the corners they arrive at without covering, and those corners are named
    // so that no cell is built at them either.
    REQUIRE(selected.size() == 4);
    CHECK(noCorner.size() == 4);
    CHECK(chainJunctions(mm, adj, selected, r, /*concave=*/false, noCorner).empty());

    const auto tool =
      buildRoundSolid(mm, adj, selected, r, /*concave=*/false, 32, 45.0, noCorner);
    REQUIRE_FALSE(tool.IsEmpty());
    // Nothing reaches further than one radius down the sides, whatever the slab.
    CHECK(tool.BoundingBox().min[2] > 10.0 - r - 1e-6);

    const auto rounded = cube - tool;
    REQUIRE_FALSE(rounded.IsEmpty());
    CHECK(rounded.Genus() == 0);
    // The vertical edge is square where the beads do not reach it.
    const auto probe = box(0.2, 0.2, 0.2).Translate(manifold::vec3(9.8, 9.8, 10.0 - 2.0 * r));
    CHECK((probe - rounded).IsEmpty());
    // And the whole solid is the same one at every slab height short of r.
    if (first == 0.0) first = rounded.Volume();
    else CHECK(rounded.Volume() == Approx(first));
  }

  // Pinned against the shape it must not be. Building a corner cell at each of
  // the four top vertices instead gives 967.4 here, so the two answers are 16
  // cubic millimetres apart and this number tells them apart.
  //
  // This pinned 983.36 before, and that number contradicts the prose above it. The
  // prose asks for the four top edges "covered end to end, so they carry full
  // beads"; a hand-built model of exactly that - four full quarter-rounds, the
  // four vertical edges left square - measures 968.5591. HEAD's 983.36 is 15.16
  // mm3 of material above that hand ideal, i.e. bead the four top edges never got,
  // and HEAD's mesh for this shape also carries a non-manifold edge. So the old
  // number pinned a defect, not the documented shape.
  //
  // 968.089 rather than the hand ideal's 968.559 because this test builds the arc
  // from 32 segments: an inscribed polygon cuts a little wider than the true
  // quarter circle. At the tessellation the hand model uses the two agree to
  // 0.018 mm3.
  CHECK(first == Approx(968.089).margin(0.05));
}

TEST_CASE("brush: a corner the brush reaches but does not cover is not built")
{
  // A corner cell is a fixed size: it is hulled from the seated ball and the
  // sections the beads stop at, and there is no perpendicular to clip it against
  // in three directions at once. So a brush that reaches the vertex by a fraction
  // of the setback and gets the whole cell is the brush contract broken by the
  // rest of it — while along an edge the same brush is honoured exactly. The rule
  // is therefore coverage of the setback, not arrival at the vertex; and short of
  // it the stretches that would have met in the cell go too, since a bead ending
  // inside the corner it was asked to close meets nothing there.
  const double r = 3.0;
  const auto cube = box(20.0, 20.0, 20.0);
  const MergedMesh mm = mergeMesh(cube.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/false));
  REQUIRE(chains.size() == 12);

  // Two boxes on the same corner, one reaching 2 mm down each edge from it and
  // one reaching 6. Both cover part of the same three creases.
  for (const double D : {2.0, 6.0}) {
    const auto brush =
      box(D + 1.0, D + 1.0, D + 1.0).Translate(manifold::vec3(-1.0, -1.0, 20.0 - D));
    auto selected = selection(mm, chains, brush, r);
    REQUIRE(selected.size() == 3);

    std::set<int> noCorner;
    const auto uncovered = dropUncoveredCorners(mm, selected, chains, r, &noCorner);
    const auto junctions = chainJunctions(mm, adj, selected, r, /*concave=*/false, noCorner);
    const auto tool =
      buildRoundSolid(mm, adj, selected, r, /*concave=*/false, 24, 45.0, noCorner);

    if (D < r) {
      REQUIRE(uncovered.size() == 1);
      CHECK(mm.pos[uncovered[0]].isApprox(Vector3d(0.0, 0.0, 20.0)));
      // Nothing at all: no cell, and none of the three stretches that ran into
      // it. Reading the emptied selection as "no brush" instead would round all
      // three creases end to end, which is thirty times the material and the
      // opposite of what was asked for.
      CHECK(selected.empty());
      CHECK(junctions.empty());
      CHECK(tool.IsEmpty());
    } else {
      REQUIRE_FALSE(tool.IsEmpty());
      CHECK(uncovered.empty());
      REQUIRE(junctions.size() == 1);
      CHECK(mm.pos[junctions[0].vert].isApprox(Vector3d(0.0, 0.0, 20.0)));
      CHECK(tool.Volume() > 30.0);
      // And the beads run out exactly as far as the brush does, not to the
      // setback: 6 mm down each edge from the vertex.
      CHECK(tool.BoundingBox().min[2] == Approx(20.0 - D).margin(1e-6));
      // The corner closes: a cube with one corner rounded is still one solid.
      const auto rounded = cube - tool;
      REQUIRE_FALSE(rounded.IsEmpty());
      CHECK(rounded.Genus() == 0);
    }
  }
}

TEST_CASE("brush: width selects the crease and does not shape the blend")
{
  // A brush clips the spine, not the section. So the blend along a selected
  // stretch is the full requested profile however narrow the brush is across the
  // crease — which is what makes a brush a selection and not a cutting tool, and
  // what lets the documentation promise that a brush can be as thin as it likes.
  const double r = 3.0;
  const auto cube = box(20.0, 20.0, 20.0);
  const MergedMesh mm = mergeMesh(cube.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/false));

  // The same 10 mm of one vertical edge, taken by a hair and by a slab 400 times
  // wider. Both stop short of either end face, so neither reaches a corner.
  double reference = 0.0;
  for (const double w : {0.02, 8.0}) {
    const auto brush = manifold::Manifold(column(w, 5.0, 15.0));
    const auto selected = brushed(mm, chains, brush, r);
    REQUIRE(selected.size() == 1);
    const auto tool = buildRoundSolid(mm, adj, selected, r, /*concave=*/false, 24, 45.0, {});
    REQUIRE_FALSE(tool.IsEmpty());
    // Square-capped at the brush at both ends, so the volume is the section area
    // times the 10 mm selected, whichever brush cut it.
    CHECK(tool.BoundingBox().min[2] == Approx(5.0).margin(1e-6));
    CHECK(tool.BoundingBox().max[2] == Approx(15.0).margin(1e-6));
    if (reference == 0.0) reference = tool.Volume();
    else CHECK(tool.Volume() == Approx(reference).epsilon(1e-9));
  }
}

TEST_CASE("brush: one whole edge and only that edge is a brush a model can draw")
{
  // Any brush tall enough to hold a full vertical edge of a cube also holds the
  // first millimetres of the four horizontal edges meeting it, so the obvious
  // brush takes 5 of 12 and not 1. What drops the four is the corner rule: those
  // stubs all start at a shared vertex, and a stub short of the radius there is
  // dropped with the corner it cannot close. The documented recipe rests on that
  // rule and not on the debounce, so a slab the model can actually draw is enough
  // — it only has to stay under the radius across the edge.
  const double r = 3.0;
  const auto cube = box(20.0, 20.0, 20.0);
  const MergedMesh mm = mergeMesh(cube.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/false));
  REQUIRE(chains.size() == 12);

  // Columns straddling one vertical edge over its whole height, of a width that
  // reaches less than r down the four neighbours and of one that reaches more.
  for (const double w : {0.02, 2.9, 8.0}) {
    const auto brush = manifold::Manifold(column(w, -1.0, 21.0));
    auto selected = selection(mm, chains, brush, r);
    // And nothing is reported at either vertex: the four stubs go because the
    // brush was aimed along the fifth crease, which is the rule doing its job
    // and not a corner anyone was denied. The count of edges taken says the
    // rest.
    CHECK(dropUncoveredCorners(mm, selected, chains, r).empty());
    if (w < 2.0 * r) {
      CHECK(takenEdges(selected) == 1);
      REQUIRE(selected.size() == 1);
      // And the one edge is blended over the whole of its height, since the brush
      // never cut it: no corner cell at either end, so the blend runs out to both
      // sharp vertices.
      const auto tool = buildRoundSolid(mm, adj, selected, r, /*concave=*/false, 24, 45.0, {});
      REQUIRE_FALSE(tool.IsEmpty());
      CHECK(tool.BoundingBox().min[2] == Approx(0.0).margin(1e-6));
      CHECK(tool.BoundingBox().max[2] == Approx(20.0).margin(1e-6));
    } else {
      // Wide enough and the four stubs cover the radius, so they are what the
      // brush asked for: five edges, and a corner cell at each end of the one.
      CHECK(takenEdges(selected) == 5);
      CHECK(chainJunctions(mm, adj, selected, r, /*concave=*/false, {}).size() == 2);
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
                       buildRoundSolid(run.mm, run.adj, run.chains, r, /*concave=*/false, 64,
                                       20.0, {});
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

TEST_CASE("size: a contact landing on the edge of its wall is a fit, not a miss")
{
  // Two bosses overlapping on a plate. Their top faces merge into one, and its
  // outline — the convex crease the round tool takes — runs round both discs and
  // through the two points where they cross. At the stations near a crossing the
  // ball's nearest point on the top face lands exactly on that outline, because
  // the outline is the boundary of the very wall being asked about, so the
  // off-face test's pass/fail line is met exactly and the last bits of a double
  // decide it.
  //
  // What that cost is a whole rim: the chain was dropped for a miss of
  // 4.4e-16 mm, one boss's top rim came back sharp, and the warning blamed a
  // radius that fits with 8 mm to spare.
  const double RB = 10.0, HB = 20.0, PT = 6.0, r = 2.0;
  const auto boss = [&](double x) {
    return manifold::Manifold::Cylinder(HB, RB, RB, 32, false)
      .Translate(manifold::vec3(x, 0.0, PT));
  };
  const auto model = box(60.0, 60.0, PT).Translate(manifold::vec3(-30.0, -30.0, 0.0)) +
                     boss(-6.0) + boss(6.0);
  const double threshold = derivedThreshold(discretizer(32));
  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, threshold, /*wantConcave=*/false));
  REQUIRE_FALSE(chains.empty());

  const auto verdicts =
    checkChainSizes(mm, adj, chains, r, /*concave=*/false, /*wedge=*/false, threshold);
  for (size_t ci = 0; ci < chains.size(); ++ci) {
    CAPTURE(ci, verdicts[ci].amount);
    CHECK(verdicts[ci].fault == SizeFault::Fits);
  }

  // And the misses that are there really are at float noise, not at a size the
  // margin has swallowed: the largest of them over the whole model is nanometres
  // of a 20 mm boss.
  const auto surfaceOf = smoothSurfaces(mm, adj, threshold);
  double worst = 0.0;
  for (const auto& chain : chains)
    for (const auto& c :
         chainContacts(mm, adj, chain, r, /*concave=*/false, /*wedge=*/false, surfaceOf, 3))
      if (c.valid) worst = std::max(worst, c.offFace);
  CHECK(worst < 1e-9);
}

TEST_CASE("apply: a bead that runs out onto a face leaves no lip over the round")
{
  // The composition fillet() is, both ways round, on the L bracket. The inner
  // bead runs the reentrant crease and stops flush in the two end faces; the
  // round pass then cuts those faces back along their outlines. Measured against
  // the original child it takes nothing off the bead's end, which is left
  // standing proud of the surface around it as a sharp crescent. Measured
  // against the blended solid, the end is part of the outline it rounds.
  const double r = 2.0;
  const double threshold = derivedThreshold(discretizer(32));
  const auto model = box(26.0, 30.0, 12.0) + box(10.0, 30.0, 26.0);

  auto roundToolOf = [&](const manifold::Manifold& target) {
    const MergedMesh tm = mergeMesh(target.GetMeshGL64());
    const auto tadj = buildEdgeAdjacency(tm.tris);
    auto chains = buildChains(tm, selectedEdges(tm, tadj, threshold, /*wantConcave=*/false));
    const auto verdicts =
      checkChainSizes(tm, tadj, chains, r, /*concave=*/false, /*wedge=*/false, threshold);
    std::vector<Chain> fitting;
    for (size_t ci = 0; ci < chains.size(); ++ci)
      if (verdicts[ci].fault == SizeFault::Fits) fitting.push_back(chains[ci]);
    CHECK(fitting.size() == chains.size());
    return buildRoundSolid(tm, tadj, fitting, r, /*concave=*/false, 32, threshold, {});
  };

  const MergedMesh mm = mergeMesh(model.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto beads = buildChains(mm, selectedEdges(mm, adj, threshold, /*wantConcave=*/true));
  const auto blended =
    model + buildRoundSolid(mm, adj, beads, r, /*concave=*/true, 32, threshold, {});

  const auto onChild = blended - roundToolOf(model);
  const auto onBlend = blended - roundToolOf(blended);

  // Both are one sound solid, and the lip is what separates them: two ends of a
  // crescent, and nothing else on the part moves.
  CHECK(onChild.Genus() == 0);
  CHECK(onBlend.Genus() == 0);
  CHECK(onChild.Volume() - onBlend.Volume() == Approx(6.568).margin(0.05));

  // The crescent is flush in the end face, so it leaves feature edges lying
  // exactly in that face's plane; rounded over, there are none. Everything else
  // the round pass makes there is tangent to the face and stays a seam.
  auto creasesInEndFace = [&](const manifold::Manifold& solid) {
    const MergedMesh sm = mergeMesh(solid.GetMeshGL64());
    const auto sadj = buildEdgeAdjacency(sm.tris);
    size_t count = 0;
    for (const auto& [key, tris] : sadj) {
      if (tris.size() != 2) continue;
      const Vector3d& p = sm.pos[key.first];
      const Vector3d& q = sm.pos[key.second];
      if (std::abs(p.y()) > 1e-9 || std::abs(q.y()) > 1e-9) continue;
      if (isFeatureAngle(classifyEdge(sm, key, sm.tris[tris[0]], sm.tris[tris[1]]).dihedralDeg,
                         threshold))
        ++count;
    }
    return count;
  };
  CHECK(creasesInEndFace(onChild) > 0);
  CHECK(creasesInEndFace(onBlend) == 0);
}

TEST_CASE("size: a bead the target already carries is not a wall in the way")
{
  // A rib on a plate, blended, and then asked about by the round pass — the
  // composition D12 wants. The blend is what makes this hard: a bead is tangent
  // to both walls it touches, so the smooth grouping cannot separate them, and
  // the whole part comes back as one surface. Asked for the nearest point of
  // "the wall" to a ball seated on the rib's top corner, an unbounded search
  // answers with the rib's other side, six millimetres away, and the crowding
  // test then refuses every one of the rib's own corners — the rounds vanish and
  // six warnings fire naming creases that have nothing wrong with them.
  const double r = 2.0;
  const double threshold = derivedThreshold(discretizer(32));
  const auto model = box(60.0, 40.0, 6.0) +
                     box(30.0, 6.0, 20.0).Translate(manifold::vec3(15.0, 17.0, 6.0));

  const MergedMesh pm = mergeMesh(model.GetMeshGL64());
  const auto padj = buildEdgeAdjacency(pm.tris);
  const auto beads = buildChains(pm, selectedEdges(pm, padj, threshold, /*wantConcave=*/true));
  const auto blend =
    model + buildRoundSolid(pm, padj, beads, r, /*concave=*/true, 32, threshold, {});

  const MergedMesh mm = mergeMesh(blend.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto surfaceOf = smoothSurfaces(mm, adj, threshold);

  // The grouping really does merge them; this is the condition being defended
  // against, not an incidental detail of the model.
  auto surfaceAt = [&](const Vector3d& p) {
    int best = -1;
    double nearest = std::numeric_limits<double>::max();
    for (size_t t = 0; t < mm.tris.size(); ++t) {
      const auto& tri = mm.tris[t];
      const double d = pointTriangleDistance(p, mm.pos[tri.v[0]], mm.pos[tri.v[1]],
                                             mm.pos[tri.v[2]]);
      if (d < nearest) { nearest = d; best = surfaceOf[t]; }
    }
    return best;
  };
  CHECK(surfaceAt({30.0, 17.0, 20.0}) == surfaceAt({30.0, 23.0, 20.0}));

  const auto chains = buildChains(mm, selectedEdges(mm, adj, threshold, /*wantConcave=*/false));
  REQUIRE_FALSE(chains.empty());
  const auto verdicts =
    checkChainSizes(mm, adj, chains, r, /*concave=*/false, /*wedge=*/false, threshold);
  for (size_t ci = 0; ci < chains.size(); ++ci) {
    CAPTURE(ci, verdicts[ci].where.x(), verdicts[ci].where.y(), verdicts[ci].where.z(),
            verdicts[ci].amount);
    CHECK(verdicts[ci].fault == SizeFault::Fits);
  }

  // And no contact of a crease on one side of the rib is seated on the other
  // side of it, which is the defect stated directly. (Both points landing on the
  // same spot is not: at a turn the ball touches the crease between two walls,
  // on the boundary of each, which is what a chain that turns is.)
  for (const auto& chain : chains)
    for (const auto& c :
         chainContacts(mm, adj, chain, r, /*concave=*/false, /*wedge=*/false, surfaceOf, 3)) {
      if (!c.valid || c.v.z() < 6.0 || c.v.x() < 15.0 || c.v.x() > 45.0) continue;
      const bool near = c.v.y() < 20.0;
      CAPTURE(c.v.x(), c.v.y(), c.v.z(), c.TA.y(), c.TB.y());
      CHECK(near == (c.TA.y() < 20.0));
      CHECK(near == (c.TB.y() < 20.0));
    }
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
TEST_CASE("size: a crease the exemptions cover entirely is still asked about")
{
  // The same needle, and a radius chosen so that the junction exemption alone
  // covers the whole crease: the three slant creases are 120 long and meet at
  // the apex, so a reach of 2 * 70 = 140 puts every sample on every one of them
  // inside it, and the two end samples are exempt as ends. Nothing is left for
  // the gate to look at.
  //
  // What the answer has to be does not depend on that. A ball of radius 70 set
  // against a needle 3 across stands off the walls it is meant to blend by 66
  // and 121 mm, on every one of the six creases, so all six have to be refused
  // and refused as OffFace. That is not the small-radius answer scaled up: at
  // radius 1 the three base creases return Fits, and only the three slant
  // creases are refused, by 0.156, 0.156 and 1.57. Half the verdicts change
  // between the two radii, and the reason to pin the large one is that it is
  // the radius at which the exemptions swallow the whole crease -- a gate that
  // returns Fits there is answering "I could not look" as if it were "it fits".

  const auto needle = manifold::Manifold::Cylinder(120.0, 3.0, 0.0, 3, false);
  const MergedMesh mm = mergeMesh(needle.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 20.0, /*wantConcave=*/false));
  REQUIRE(chains.size() == 6);

  const auto verdicts = checkChainSizes(mm, adj, chains, 70.0, false, false, 20.0);
  CHECK(countFault(verdicts, SizeFault::OffFace) == chains.size());
  // And by an amount worth the name. The needle is 3 across and 120 long, so a
  // blend of 70 misses its walls by tens of millimetres; a fault reported at a
  // fraction of the size would mean the gate had found float noise rather than
  // the miss.
  for (const auto& v : verdicts) CHECK(v.amount > 70.0 * 0.5);
}

TEST_CASE("size: a crease the exemptions cover is judged the same at any scale")
{
  // The same needle and the same radius as above, at one millionth of the size.
  // A modeller without units has no size at which its answers may change, and
  // nothing about this question depends on one: the two solids are similar, so
  // a blend of 70e-6 on a needle 3e-6 across stands off its walls by exactly a
  // millionth of what a blend of 70 on a needle 3 across stands off by, and all
  // six creases have to be refused for the same reason and by that amount.
  //
  // The expectation is derived from that similarity rather than from what the
  // gate emits: the scaled amounts are checked against the unit-scale ones,
  // measured here in the same test, times the scale. The unit-scale ones are
  // themselves the hand-built answer pinned above -- 66.43 on the three base
  // creases and 121.09 on the three slant ones -- so both ends of the ratio are
  // tied to the needle's proportions and neither is a reading of the output.
  //
  // What this catches is a margin expressed as an absolute number of
  // millimetres. Such a floor does not shrink with the model, so a millionth
  // scale puts the whole of the evidence underneath it and every crease comes
  // back Fits -- which is not an academic case: `fillet()` on crossing pipes
  // at that scale returned a solid in 19 separate pieces while the same model
  // at unit scale was sound, and the gate made no refusal at all to say so.

  const auto needle = manifold::Manifold::Cylinder(120.0, 3.0, 0.0, 3, false);
  const MergedMesh mm = mergeMesh(needle.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, 20.0, /*wantConcave=*/false));
  REQUIRE(chains.size() == 6);
  const auto unit = checkChainSizes(mm, adj, chains, 70.0, false, false, 20.0);
  REQUIRE(countFault(unit, SizeFault::OffFace) == 6);

  const double k = 1e-6;
  const auto small = needle.Scale(manifold::vec3(k, k, k));
  const MergedMesh sm = mergeMesh(small.GetMeshGL64());
  const auto sadj = buildEdgeAdjacency(sm.tris);
  const auto schains = buildChains(sm, selectedEdges(sm, sadj, 20.0, /*wantConcave=*/false));
  REQUIRE(schains.size() == 6);
  const auto scaled = checkChainSizes(sm, sadj, schains, 70.0 * k, false, false, 20.0);

  CHECK(countFault(scaled, SizeFault::OffFace) == 6);
  std::vector<double> want, got;
  for (const auto& v : unit) want.push_back(v.amount * k);
  for (const auto& v : scaled) got.push_back(v.amount);
  std::sort(want.begin(), want.end());
  std::sort(got.begin(), got.end());
  REQUIRE(want.size() == got.size());
  for (size_t i = 0; i < want.size(); ++i) CHECK(got[i] == Approx(want[i]).epsilon(1e-6));

  // And the shape of the answer is the same too: three creases at the smaller
  // amount and three at the larger, not six that happen to average right.
  CHECK(got[0] == Approx(66.4313 * k).epsilon(1e-4));
  CHECK(got[5] == Approx(121.0920 * k).epsilon(1e-4));
}

TEST_CASE("size: a spine that turns from one wall onto the next still meets both")
{
  // An L bracket, and the crease that matters is not the reflex one. Round the
  // end face's outline and the spine turns 90 degrees at the L's reentrant
  // corner, from the wall above it onto the wall beside it. The ball seated
  // there is seated on the average of two walls' normals, so the point it
  // touches on either one of them is the crease *between* them — on the
  // boundary of each, exactly and at every radius, which is what the touching
  // question calls a wall running out. It is the ball rolling from one wall
  // onto the next instead, and nothing about the size is wrong: both radii fit
  // on both proportions, and the reflex crease they meet is untouched.
  for (const auto& wh : {std::pair{10.0, 26.0}, std::pair{6.0, 26.0}}) {
    const auto bracket = box(30.0, wh.first, wh.second) + box(30.0, 26.0, 12.0);
    for (const double r : {1.0, 2.0}) {
      const SizeRun outer = sizeRun(bracket, r, /*concave=*/false, /*wedge=*/false, 18.0);
      CHECK(countFault(outer.verdicts, SizeFault::OffFace) == 0);
      const SizeRun inner = sizeRun(bracket, r, /*concave=*/true, /*wedge=*/false, 18.0);
      REQUIRE(inner.chains.size() == 1);
      CHECK(inner.verdicts[0].fault == SizeFault::Fits);
    }
  }

  // And the exemption is for the turn and not for the chain: a spine turning at
  // a corner of a wall that genuinely has run out is still refused, at the
  // samples either side of the turn, which each ask about one wall.
  const auto ledge = box(30.0, 10.0, 13.0) + box(30.0, 26.0, 12.0);
  const SizeRun over = sizeRun(ledge, 2.0, /*concave=*/false, /*wedge=*/false, 18.0);
  CHECK(countFault(over.verdicts, SizeFault::OffFace) > 0);
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

  const auto junctions = chainJunctions(mm, adj, chains, r, /*concave=*/false, {});
  const Junction *apex = nullptr;
  for (const auto& j : junctions)
    if (mm.pos[j.vert].z() > 119.0) apex = &j;
  REQUIRE(apex != nullptr);
  CHECK(apex->ballCentres.empty());

  const auto tool = buildRoundSolid(mm, adj, chains, r, /*concave=*/false, 24, 20.0, {});
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

  const auto junctions = chainJunctions(mm, adj, chains, r, /*concave=*/false, {});
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

  const auto junctions = chainJunctions(mm, adj, chains, r, /*concave=*/false, {});
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

  for (const auto& j : chainJunctions(mm, adj, chains, 1.0, /*concave=*/true, {}))
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

TEST_CASE("curved crease: the bead comes back whole at every tessellation")
{
  // What a bead along a curved crease is topologically, asked of enough
  // configurations that a defect which only shows at some of them cannot hide.
  //
  // The one that used to hide there is the station seam: consecutive cells of a
  // chain abut on a shared face, that face is always the plane of a station, and
  // the wedges and the canals are cut at the same stations - so the boolean is
  // handed a pair of coincident planes at every one of them and comes down on
  // the wrong side of the odd one. Nothing about the *shape* decides which: on a
  // plain boss it moved with the facet count, with the radius, and with where on
  // the plate the boss was standing, so no single model pins it and a sweep is
  // the only honest test.
  //
  // Both of its outcomes are checked, because neither sees the other. A slit
  // through the bead is a hole, which genus sees: a closed ring of bead is a
  // torus and a cylinder with both rims rounded is still a ball. A seam left
  // standing is a flap of zero thickness, which genus does not see at all -
  // Manifold calls such a solid sound - so the mesh is asked directly whether it
  // touches itself, which is also what a convex decomposition downstream will
  // refuse.
  //
  // The arc is tessellated the way the node does it - off the same $fn the model
  // was built at - rather than at some fixed count, because a bead drawn finer
  // than the wall it sits on is not a configuration the operator can produce.
  auto selfTouching = [](const manifold::Manifold& solid) {
    const MergedMesh sm = mergeMesh(solid.GetMeshGL64());
    size_t count = 0;
    for (const auto& [key, tris] : buildEdgeAdjacency(sm.tris))
      if (tris.size() != 2) ++count;
    return count;
  };

  auto toolFor = [&](const manifold::Manifold& model, double r, bool concave, double threshold,
                     int segs) {
    const MergedMesh mm = mergeMesh(model.GetMeshGL64());
    const auto adj = buildEdgeAdjacency(mm.tris);
    const auto chains = buildChains(mm, selectedEdges(mm, adj, threshold, concave));
    return buildRoundSolid(mm, adj, chains, r, concave, segs, threshold, {});
  };

  SECTION("a boss on a plate: the base fillet is one closed ring")
  {
    for (const int fn : {24, 32, 48, 64}) {
      const double threshold = derivedThreshold(discretizer(fn));
      for (const double r : {1.0, 2.0, 3.0}) {
        const int segs = discretizer(fn).getCircularSegmentCount(r).value_or(32);
        // Where the boss stands is a coordinate and nothing else, so it must not
        // change the answer. It did.
        for (const double at : {20.0, 25.0, 30.0, 33.0, 38.0}) {
          const auto model =
            box(60.0, 40.0, 6.0) +
            manifold::Manifold::Cylinder(16.0, 10.0, 10.0, fn, false)
              .Translate(manifold::vec3(at, 20.0, 6.0));
          const auto tool = toolFor(model, r, /*concave=*/true, threshold, segs);
          CAPTURE(fn, r, at);
          REQUIRE_FALSE(tool.IsEmpty());
          CHECK(tool.Genus() == 1);
          CHECK(selfTouching(tool) == 0);
          CHECK(selfTouching(model + tool) == 0);
        }
      }
    }
  }

  SECTION("a cylinder with both rims rounded is still a ball")
  {
    for (const int fn : {24, 32, 48, 64}) {
      const double threshold = derivedThreshold(discretizer(fn));
      for (const double r : {1.0, 2.0, 3.0}) {
        const int segs = discretizer(fn).getCircularSegmentCount(r).value_or(32);
        for (const double at : {0.0, 10.0, 22.0, 30.0}) {
          const auto model = manifold::Manifold::Cylinder(16.0, 10.0, 10.0, fn, false)
                               .Translate(manifold::vec3(at, 20.0, 0.0));
          const auto tool = toolFor(model, r, /*concave=*/false, threshold, segs);
          CAPTURE(fn, r, at);
          REQUIRE_FALSE(tool.IsEmpty());
          CHECK((model - tool).Genus() == 0);
          CHECK(selfTouching(tool) == 0);
          CHECK(selfTouching(model - tool) == 0);
        }
      }
    }
  }
}

TEST_CASE("junction: two beads left by a refused crease do not touch")
{
  // A pipe running out of a boss on a plate. Three creases meet where the two
  // cylinders cross the plate, and the short one between them is refused for
  // size - so no corner is built there, and the two beads that were built still
  // arrive at the same point. Stopping both on it leaves them touching along one
  // line, which the caller's union resolves into a flap of zero thickness in its
  // result: the tool alone is sound, and only the union shows it.
  //
  // Genus does not see a flap, so the mesh is asked directly. The bare crease
  // between the two beads is bare either way - it is the refused crease's own
  // corner, which nothing was going to blend.
  auto selfTouching = [](const manifold::Manifold& solid) {
    const MergedMesh sm = mergeMesh(solid.GetMeshGL64());
    size_t count = 0;
    for (const auto& [key, tris] : buildEdgeAdjacency(sm.tris))
      if (tris.size() != 2) ++count;
    return count;
  };

  for (const int fn : {24, 32}) {
    const double threshold = derivedThreshold(discretizer(fn));
    for (const double r : {0.5, 0.6, 0.9, 1.0}) {
      for (const double at : {20.0, 33.0, 38.0}) {
        const auto model = box(60.0, 40.0, 6.0) +
                           manifold::Manifold::Cylinder(16.0, 8.0, 8.0, fn, false)
                             .Translate(manifold::vec3(at, 20.0, 6.0)) +
                           manifold::Manifold::Cylinder(30.0, 5.0, 5.0, fn, false)
                             .Rotate(0, 90, 0)
                             .Translate(manifold::vec3(at, 20.0, 10.0));
        const MergedMesh mm = mergeMesh(model.GetMeshGL64());
        const auto adj = buildEdgeAdjacency(mm.tris);
        auto chains = buildChains(mm, selectedEdges(mm, adj, threshold, /*wantConcave=*/true));
        const auto verdicts =
          checkChainSizes(mm, adj, chains, r, /*concave=*/true, /*wedge=*/false, threshold);

        // The case is only itself while one crease is refused and the others are
        // not.
        size_t refused = 0;
        std::vector<Chain> fitting;
        for (size_t ci = 0; ci < chains.size(); ++ci) {
          if (verdicts[ci].fault == SizeFault::Fits) fitting.push_back(chains[ci]);
          else ++refused;
        }
        CAPTURE(fn, r, at);
        REQUIRE(refused == 1);
        REQUIRE(fitting.size() == 2);

        const int segs = discretizer(fn).getCircularSegmentCount(r).value_or(32);
        const auto tool =
          buildRoundSolid(mm, adj, fitting, r, /*concave=*/true, segs, threshold, {});
        REQUIRE_FALSE(tool.IsEmpty());
        CHECK(selfTouching(tool) == 0);
        CHECK(selfTouching(model + tool) == 0);
      }
    }
  }
}

TEST_CASE("junction: a corner two creases still reach is closed at every opening angle")
{
  // The same defect on the plainest shape that has it, and the measurement that
  // says it is generic rather than a property of the pipe tee. Two flat bars on a
  // plate meeting at a settable angle: three concave creases arrive at the inner
  // vertex, and dropping the vertical one leaves the two the plate carries
  // arriving together, the way a crease refused for size leaves them.
  //
  // Both beads are tangent to the plate, so their footprints on it cross about a
  // radius out from the vertex and the two surfaces meet there at no angle at
  // all. Measured with the corner left unbuilt, that shows up at 60, 75, 105 and
  // 120 degrees. Ninety comes back clean on its own, because there the two beads
  // are mirror images and their intersection lands on the symmetry plane - which
  // is a property of the mesh rather than of the shape, and the reason the rule
  // is not an angle.
  auto selfTouching = [](const manifold::Manifold& solid) {
    const MergedMesh sm = mergeMesh(solid.GetMeshGL64());
    size_t count = 0;
    for (const auto& [key, tris] : buildEdgeAdjacency(sm.tris))
      if (tris.size() != 2) ++count;
    return count;
  };

  for (const double theta : {60.0, 75.0, 90.0, 105.0, 120.0}) {
    for (const double r : {0.5, 1.0, 2.0}) {
      const auto plate = box(80.0, 80.0, 4.0).Translate(manifold::vec3(-40.0, -40.0, 0.0));
      const auto bar = box(30.0, 4.0, 12.0).Translate(manifold::vec3(0.0, -2.0, 4.0));
      const auto model = plate + bar + bar.Rotate(0, 0, theta);
      const MergedMesh mm = mergeMesh(model.GetMeshGL64());
      const auto adj = buildEdgeAdjacency(mm.tris);
      const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*wantConcave=*/true));

      // Everything but the vertical reflex edge, which stands in for the crease
      // a size gate would have dropped.
      std::vector<Chain> flat;
      for (const Chain& c : chains) {
        int endFront = -1, endBack = -1;
        if (!c.openEnds(endFront, endBack)) continue;
        if (std::abs(mm.pos[endFront].z() - mm.pos[endBack].z()) > 1.0) continue;
        flat.push_back(c);
      }
      CAPTURE(theta, r);
      REQUIRE(flat.size() == 2);

      const auto tool = buildRoundSolid(mm, adj, flat, r, /*concave=*/true, 32, 45.0, {});
      REQUIRE_FALSE(tool.IsEmpty());
      CHECK(selfTouching(tool) == 0);
      CHECK(selfTouching(model + tool) == 0);
    }
  }
}

TEST_CASE("curved crease: a uniform seam gives a uniform setback")
{
  // Where a cylinder stands on a plate the crease is a circle, every station on
  // it is the same station turned, and the setback the tool takes along each
  // wall is therefore one number for the whole ring. Anything read off the mesh
  // that varies from station to station - a wall normal taken off the triangles
  // carrying the crease, above all - shows up here as a setback that wobbles,
  // and a wobbling setback is a bead with a scalloped edge.
  //
  // Measured against the arc-length interpolation of each station's neighbours,
  // so that uneven spacing is not read as wobble. It is not the metric that
  // makes this exact: the ring is evenly spaced. A seam between two curved walls
  // is neither, which is where the wobble that is still open lives.
  const double r = 2.0;
  for (const int fn : {24, 48, 96}) {
    const double threshold = derivedThreshold(discretizer(fn));
    const auto model = box(60.0, 40.0, 6.0) +
                       manifold::Manifold::Cylinder(16.0, 10.0, 10.0, fn, false)
                         .Translate(manifold::vec3(25.0, 20.0, 6.0));
    const MergedMesh mm = mergeMesh(model.GetMeshGL64());
    const auto adj = buildEdgeAdjacency(mm.tris);
    const auto chains = buildChains(mm, selectedEdges(mm, adj, threshold, /*wantConcave=*/true));
    REQUIRE(chains.size() == 1);

    const auto frames = spineFrames(mm, adj, chains[0], r, /*concave=*/true);
    std::vector<double> back;
    for (const auto& f : frames)
      if (f.valid) back.push_back((f.TA - f.v).norm());
    REQUIRE(back.size() == static_cast<size_t>(fn));

    double mean = 0.0, worst = 0.0;
    for (const double b : back) mean += b;
    mean /= static_cast<double>(back.size());
    for (size_t i = 0; i < back.size(); ++i) {
      const double before = back[(i + back.size() - 1) % back.size()];
      const double after = back[(i + 1) % back.size()];
      worst = std::max(worst, std::abs(back[i] - 0.5 * (before + after)));
    }
    CAPTURE(fn, mean);
    CHECK(worst < 1e-9 * mean);
  }
}

// The station list is not the crease, and this is the only test that can tell.
//
// Every other chain in this file comes straight from buildChains, where exactly
// one station stands on each crease vertex, so the two lists agree and a
// consumer that confuses them still passes. That is how the same fault reached
// four separate consumers -- checkChainSizes, filletedEdges, chainJunctions and
// a seam predicate since removed -- and was caught four times by hand and never
// here. These are
// the invariants those consumers were relying on, stated once.
TEST_CASE("resampling: a station is not a crease vertex, and the ends still are")
{
  // A bore through a round bar. Where the two tessellations cross, the crease
  // they share has segments a small fraction of its own median, which is the
  // case the resampler exists for; a hole in a flat plate has none and is never
  // resampled at all.
  constexpr int FN = 96;
  const auto bar = manifold::Manifold::Cylinder(40.0, 10.0, 10.0, FN, false);
  const auto bore = manifold::Manifold::Cylinder(40.0, 4.0, 4.0, FN, false)
                      .Translate(manifold::vec3(0.0, 0.0, -20.0))
                      .Rotate(-90, 0, 0)
                      .Translate(manifold::vec3(0.0, 0.0, 20.0));
  const MergedMesh mm = mergeMesh((bar - bore).GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const double threshold = derivedThreshold(discretizer(FN));

  // The two bore mouths: convex rims, one ring each. The bar's own two end
  // rims are rings too, and regular ones -- the resampler leaves them alone,
  // which is what the constant-z test picks out.
  auto chains = buildChains(mm, selectedEdges(mm, adj, threshold, /*wantConcave=*/false));
  std::vector<Chain> rings;
  for (const Chain& c : chains) {
    if (!c.closed) continue;
    double lo = 1e30, hi = -1e30;
    for (const int v : c.rawRun()) {
      lo = std::min(lo, mm.pos[v].z());
      hi = std::max(hi, mm.pos[v].z());
    }
    if (hi - lo > 1e-9) rings.push_back(c);
  }
  REQUIRE(rings.size() == 2);

  // An open chain to check the other half of the rule on: cut the same rings at
  // a vertex by taking the crease's own run and reopening it. Rebuilding one by
  // hand is the only way to get an open chain that is also slivered enough to be
  // resampled, since a bore mouth is always a ring.
  std::vector<Chain> opened;
  for (const Chain& c : rings) {
    Chain o;
    o.raw = c.rawRun();
    o.setStations(o.raw);
    o.closed = false;
    opened.push_back(std::move(o));
  }

  std::vector<Chain> all = rings;
  all.insert(all.end(), opened.begin(), opened.end());
  const std::vector<int> rawBefore0 = all.front().rawRun();

  resampleChains(mm, all, kSliverFraction);

  // The mechanism fired. `at` is empty on a chain the resampler left alone, so a
  // pass that measured nothing would fail here rather than below.
  size_t redivided = 0;
  for (const Chain& c : all)
    if (!c.at.empty()) ++redivided;
  CHECK(redivided == all.size());

  // I3 -- the crease is untouched by resampling. Every -1 guard behind rawRun()
  // is dead code, which is why filletedEdges does not carry one.
  CHECK(all.front().rawRun() == rawBefore0);

  for (size_t i = 0; i < all.size(); ++i) {
    const Chain& c = all[i];
    CAPTURE(i, c.closed);

    // I2 -- station count equals crease length. Three size-based consumers
    // depend on this and nothing in the type enforces it: the resampler happens
    // to emit as many stations as the crease has segments. If that ever changes,
    // this fails, and every stationCount() in the builder has to be re-read.
    CHECK(c.stationCount() == c.rawCount());

    // I1 -- an OPEN chain's two ends are exact mesh vertices, resampled or not.
    // Corner cells and brush coverage are keyed by them.
    int endFront = -1, endBack = -1;
    if (c.openEnds(endFront, endBack)) {
      for (const bool front : {true, false}) {
        const int v = front ? endFront : endBack;
        REQUIRE(v >= 0);
        REQUIRE(static_cast<size_t>(v) < mm.pos.size());
        const int station = front ? 0 : c.stationCount() - 1;
        CHECK(c.param(station) == Approx(front ? 0.0 : static_cast<double>(c.rawCount() - 1)));
        CHECK(c.point(mm.pos, station).isApprox(mm.pos[v]));
      }
    }

    // A ring has ONE pinned station and it is station 0. Its last station is
    // placed by arc length like any interior one, so it stands between two mesh
    // vertices and has no vertex of its own -- which is why openEnds() answers
    // false for a ring rather than handing back the -1 that used to be there.
    if (c.closed) {
      int ringFront = -1, ringBack = -1;
      CHECK_FALSE(c.openEnds(ringFront, ringBack));
      CHECK(c.param(0) == Approx(0.0));
      CHECK(c.point(mm.pos, 0).isApprox(mm.pos[c.rawRun().front()]));
      CHECK(c.param(c.stationCount() - 1) > static_cast<double>(c.rawCount() - 1));
    }

    // Interpolated stations exist, and they are exactly the ones whose parameter
    // is not a whole crease vertex. That is the condition under which the list
    // holds -1, said in the vocabulary the chain still exposes.
    size_t interior = 0;
    for (int k = 0; k < c.stationCount(); ++k) {
      const double p = c.param(k);
      if (p > std::floor(p)) ++interior;
      // Whatever the station is, its position is the point its own parameter
      // names on the crease. `pts`, `at` and the station list are one list.
      const int j = static_cast<int>(std::floor(p));
      const double f = p - static_cast<double>(j);
      const std::vector<int>& run = c.rawRun();
      const Vector3d a = mm.pos[run[j % c.rawCount()]];
      const Vector3d b = mm.pos[run[(j + 1) % c.rawCount()]];
      CHECK((c.point(mm.pos, k) - (a + f * (b - a))).norm() < 1e-9);
    }
    CHECK(interior > 0);
  }
}

#endif  // ENABLE_MANIFOLD
