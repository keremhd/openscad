#pragma once

// Internal decomposition of the fillet edge-classification pass. These types and
// helpers back buildFilletTool() and are exposed here (rather than living in an
// anonymous namespace in the .cc) so the unit test can exercise the pure mesh
// combinatorics — vertex merging, edge adjacency, concavity — directly, without
// going through the geometry evaluator. Not part of the public fillet API.

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include <manifold/manifold.h>

#include "geometry/linalg.h"

namespace fillet::detail {

// One mesh triangle after MeshGL's per-run vertex duplicates have been merged by
// position: the three topological (merged) vertex indices, the outward face
// normal, and the source-surface id Manifold propagates through booleans.
struct Tri
{
  std::array<int, 3> v;
  Vector3d normal;
  uint32_t originalID;
};

// Undirected edge, endpoints stored min-first so both winding directions land on
// the same key.
using EdgeKey = std::pair<int, int>;

// The target mesh reduced to topological form: merged vertex positions and
// triangles carrying merged indices, plus the set of distinct source ids.
struct MergedMesh
{
  std::vector<Vector3d> pos;       // indexed by merged vertex id
  std::vector<Tri> tris;           // merged indices, outward normals
  std::size_t numRawVert = 0;      // pre-merge vertex count (diagnostic)
  std::set<uint32_t> distinctIDs;  // source surfaces present
};

// Merge MeshGL vertices by exact position and build the triangle list. A single
// spatial vertex is emitted once per run it touches (runs differ by surface id),
// so raw indices do not give topological adjacency until coincident positions
// are unified.
MergedMesh mergeMesh(const manifold::MeshGL64& mesh);

// Rebuild edge -> incident-triangle adjacency from the triangle soup. A manifold
// mesh yields exactly two triangles per edge.
std::map<EdgeKey, std::vector<int>> buildEdgeAdjacency(const std::vector<Tri>& tris);

// Classification of one two-face edge: the dihedral angle between its faces and
// whether the crease is concave (inner corner) or convex (outer corner).
struct EdgeClass
{
  double dihedralDeg;
  bool concave;
};

// Classify a single two-face edge from its two incident triangles. Concavity
// uses the far-vertex test (does A's far corner poke in front of B's plane)
// because dot(nA, nB) alone cannot separate an inner corner from an outer one.
EdgeClass classifyEdge(const MergedMesh& m, const EdgeKey& key, const Tri& A, const Tri& B);

// Aggregate counts over all edges, applying the crease threshold to separate
// feature edges from tessellation seams.
struct ClassCounts
{
  std::size_t twoFace = 0;
  std::size_t nonManifold = 0;
  std::size_t feature = 0;
  std::size_t featureConcave = 0;
  std::size_t featureConvex = 0;
  std::size_t featureSameSurface = 0;
};

// Walk the adjacency, classify every two-face edge, and tally the counts. Edges
// with a dihedral below thresholdDeg are treated as seams and skipped. Same-
// source-id ("same-surface") edges are only tallied when useProvenance is set
// (more than one source id present); provenance is reported, not used to reject.
ClassCounts classifyEdges(const MergedMesh& m,
                          const std::map<EdgeKey, std::vector<int>>& adj,
                          double thresholdDeg, bool useProvenance);

}  // namespace fillet::detail
