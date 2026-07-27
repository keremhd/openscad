// Unit tests for the fillet edge-classification internals: MeshGL vertex
// merging, edge -> two-face adjacency, and concave/convex classification. These
// exercise the pure combinatorics directly on hand-built Manifold primitives,
// where the correct answer is a count or a boolean — the cases a rendered image
// reports poorly (a concavity sign flip is invisible until it becomes a gouge; a
// seam that leaks past the angle filter is one stray edge among hundreds).

#include <catch2/catch_all.hpp>

#ifdef ENABLE_MANIFOLD

#include "geometry/fillet/FilletBuilder_internal.h"

#include <cstddef>
#include <map>
#include <vector>

#include <manifold/manifold.h>

using namespace fillet::detail;

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

#endif  // ENABLE_MANIFOLD
