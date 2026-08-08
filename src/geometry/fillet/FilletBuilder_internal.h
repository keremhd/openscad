/*
 *  OpenSCAD (www.openscad.org)
 *  Copyright The OpenSCAD Developers.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this program; if not, see
 *  <https://www.gnu.org/licenses/>.
 */
#pragma once

// Internal decomposition of the fillet edge-classification pass. Not part of the
// public fillet API; declared here rather than in an anonymous namespace so the
// unit test can exercise the mesh combinatorics without the geometry evaluator.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
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

// Whether an edge turning dihedralDeg counts as a feature rather than a seam the
// tessellation produced. Classification, selection, smooth-surface grouping and
// debug colouring must all go through this, or an edge could be a crease to one
// and a flat seam to another.
//
// Ties are rejected. min_angle= can name an angle a surface's own facets turn by,
// and there the computed dihedrals land a few ulp either side of the threshold,
// splitting edges that are identical by symmetry. Rejecting keeps a prism a
// prism. The 1e-9 margin is four orders above that ulp noise and far below any
// angle a caller chose on purpose.
inline bool isFeatureAngle(double dihedralDeg, double thresholdDeg)
{
  return dihedralDeg >= thresholdDeg * (1 + 1e-9);
}

// The dihedral above which an edge is a feature rather than a curve-tessellation
// seam, when min_angle= names no other value. A constant, not a quantity read off
// the mesh or off $fn/$fa, so classification is a property of the solid alone —
// the same at stock defaults, at an explicit $fn, or after an STL round trip.
//
// Bounded on both sides by measurement. Below: the wall seams of cylinder($fn=8)
// turn 45, and a threshold at or under that rounds every facet of an octagonal
// prism (at exactly 45 the outcome turns on the tie margin, and measured facet
// angles are not exact — a sphere's latitude rings read ~0.065 deg off nominal).
// Above: a tee of two equal cylinders has a concave intersection curve with
// dihedrals 15.86, 46.26, 72.02, 87.82, and a threshold over 46.27 drops the
// 46.26 quartet, cutting a 16-edge chain into seven fragments of which four are
// then too short for the size gate. 46 sits in the widest gap available.
inline constexpr double kDefaultCreaseThresholdDeg = 46.0;

// The dihedral below which two triangles are treated as one smooth surface — a
// separate question from which edges are features (kDefaultCreaseThresholdDeg).
// Grouping is near-tangency and independent of min_angle: a feature edge decides
// what to fillet, this decides which triangles share a wall. Kept well below the
// crease threshold so a sub-crease seam that is not near-tangent — the tangent
// gap of a tee/cross, ~15.86 deg — is a surface boundary, not a merge, and the
// two walls stay distinct. A coarse faceted prism therefore comes out as
// separate flat surfaces, which is what it is. A named constant now; a candidate
// to expose later.
inline constexpr double kDefaultSurfaceThresholdDeg = 10.0;

// Walk the adjacency, classify every two-face edge, and tally the counts. Edges
// with a dihedral below thresholdDeg are treated as seams and skipped. Same-
// source-id ("same-surface") edges are only tallied when useProvenance is set
// (more than one source id present); provenance is reported, not used to reject.
ClassCounts classifyEdges(const MergedMesh& m,
                          const std::map<EdgeKey, std::vector<int>>& adj,
                          double thresholdDeg, bool useProvenance);

// Group the triangles into surfaces: two triangles belong to the same surface
// when the edge between them is a seam rather than a crease, i.e. when its
// dihedral falls below the same threshold that separates feature edges from
// tessellation. Returns one surface id per triangle. A bore's facets come out as
// one surface, a cube's six faces as six.
std::vector<int> smoothSurfaces(const MergedMesh& m,
                                const std::map<EdgeKey, std::vector<int>>& adj,
                                double thresholdDeg);

}  // namespace fillet::detail
