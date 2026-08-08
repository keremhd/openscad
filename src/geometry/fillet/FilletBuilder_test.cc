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
#include <limits>
#include <map>
#include <set>
#include <vector>

#include <manifold/manifold.h>

using namespace fillet::detail;
using Catch::Approx;

namespace {

// Classify a manifold at a given crease threshold: merge, rebuild adjacency,
// tally the concave/convex feature edges.
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

// Classify at the threshold the operator applies with no min_angle= named.
ClassCounts classifyAuto(const manifold::Manifold& m)
{
  return classify(m, kDefaultCreaseThresholdDeg);
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

#endif  // ENABLE_MANIFOLD
