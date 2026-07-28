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

// Internal decomposition of the fillet edge-classification pass. These types and
// helpers back buildFilletTool() and are exposed here (rather than living in an
// anonymous namespace in the .cc) so the unit test can exercise the pure mesh
// combinatorics — vertex merging, edge adjacency, concavity — directly, without
// going through the geometry evaluator. Not part of the public fillet API.

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

class PolySet;

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

// The edges this tool acts on: two-face edges whose dihedral clears thresholdDeg
// and whose concavity matches the tool (concave for fillet/chamfer, convex for
// round/bevel). These are the edges walked into chains.
std::vector<EdgeKey> selectedEdges(const MergedMesh& m,
                                   const std::map<EdgeKey, std::vector<int>>& adj,
                                   double thresholdDeg, bool wantConcave);

// A spine: an ordered run of merged vertex indices along one crease. `closed`
// marks a ring (the hole-mouth case), where the last vertex reconnects to the
// first. Ordering is canonical (derived from vertex positions, not mesh
// traversal order) so downstream indices stay put under a small parameter nudge.
struct Chain
{
  std::vector<int> verts;
  bool closed = false;
};

// Walk the selected edges into chains via shared vertices. Vertices of degree 2
// are interior stations; degree 1 are open ends; degree >= 3 are branch/junction
// vertices where chains terminate. Returns chains in canonical order.
std::vector<Chain> buildChains(const MergedMesh& m, const std::vector<EdgeKey>& edges);

// The two wall normals at one chain station, averaged over the station's
// incident chain edges. `valid` is false where the station has no usable pair
// (a dangling edge, a degenerate triangle).
struct StationNormals
{
  Vector3d v;
  Vector3d nA, nB;
  bool valid = false;
};

// Averaged outward wall normals at every vertex of a chain. Averaging per side
// across the incident chain edges is what keeps the derived spine continuous
// around a bend; side A/B is kept consistent along the chain by a fixed
// handedness relative to the walking direction, so the two sums never mix walls.
std::vector<StationNormals> chainNormals(const MergedMesh& m,
                                         const std::map<EdgeKey, std::vector<int>>& adj,
                                         const Chain& chain);

// The tangency frame at one spine station: the averaged wall normals nA/nB, the
// ball center C, the two tangency points TA/TB where radius-r arcs meet each
// wall, and the wall half-angle. `valid` is false when the crease flattens
// (phi -> 180), where the ball center runs to infinity and must be skipped.
struct SpineFrame
{
  Vector3d v;
  Vector3d nA, nB;
  Vector3d C, TA, TB;
  double phiDeg = 0.0;
  bool valid = false;
};

// Compute the tangency frame at every vertex of a chain for radius r. Wall
// normals are averaged per side across the vertex's incident chain edges (what
// keeps the ball-center polyline continuous); side A/B is kept consistent along
// the chain by a fixed handedness relative to the walking direction.
//
// The ball rolls on the material side of the crease, and which side that is
// follows the tool: at a concave edge both outward normals point into the open
// quadrant, so the center offsets along +(nA+nB) and the tangency points come
// back toward the walls; at a convex edge the ball sits inside the solid and
// both signs flip.
std::vector<SpineFrame> spineFrames(const MergedMesh& m,
                                    const std::map<EdgeKey, std::vector<int>>& adj,
                                    const Chain& chain, double r, bool concave);

// The chamfer/bevel cross-section at one chain station: the convex pentagon
// TA, TB, TB', v', TA'. TA and TB are the setback points on the two walls; the
// primed points are the same corner pushed a hair past the walls (into material
// for a concave tool, into air for a convex one) so the tool crosses each wall
// transversally instead of lying coplanar with it. Convex, so hulling two
// consecutive sections is faithful.
struct WedgeSection
{
  std::array<Vector3d, 5> p;
  bool valid = false;
};

// The wedge section at every vertex of a chain, for setback t. The setback is
// taken directly along the in-wall directions rather than derived from a radius:
// t = r*tan(phi/2) would make the number mean an inscribed radius, which is not
// what a chamfer of t means. Sign follows the tool: a concave tool sets back
// into the reentrant quadrant, a convex one into the solid.
std::vector<WedgeSection> wedgeSections(const MergedMesh& m,
                                        const std::map<EdgeKey, std::vector<int>>& adj,
                                        const Chain& chain, double t, bool concave);

// Build the chamfer/bevel tool solid: hull each consecutive pair of wedge
// sections into one cell, then union the cells. Junction cells are not built
// yet, so chains meeting at a branch vertex simply overlap there. Returns an
// empty manifold when nothing could be built.
manifold::Manifold buildWedgeSolid(const MergedMesh& m,
                                   const std::map<EdgeKey, std::vector<int>>& adj,
                                   const std::vector<Chain>& chains, double t, bool concave);

// The two cross-sections of a rounded tool at one chain station. `w` is the same
// pentagon the chamfer uses, but with its setback taken at the tangency points
// of a radius-r ball rather than from a free parameter, so the triangle
// (v, TA, TB) is exactly the corner the arc is inscribed in. `u` is the region
// that arc cuts back out of it, as a convex polygon in the plane spanned by the
// two wall normals; hulling consecutive copies of each and subtracting gives the
// rounded bead.
struct RoundSection
{
  std::array<Vector3d, 5> w;
  std::vector<Vector3d> u;
  bool valid = false;
};

// The rounded cross-sections along a chain, for radius r. The wedge setback is
// r*tan(phi/2) at each station, which is where a ball of radius r seated in the
// crease touches each wall, so it varies with the local wall angle instead of
// being one number for the chain. `arcSegments` is the tessellation of a full
// circle at this radius; the arc actually emitted spans only phi of it.
std::vector<RoundSection> roundSections(const MergedMesh& m,
                                        const std::map<EdgeKey, std::vector<int>>& adj,
                                        const Chain& chain, double r, bool concave,
                                        int arcSegments);

// Build the fillet/round tool solid: per chain, the wedge W hulled from
// consecutive sections minus the canal U hulled the same way, then the chains
// unioned. Subtraction is per chain rather than per cell (a cell's arc has to
// cut its neighbour's wedge wherever the spine bends) and rather than once
// globally (a chain's ball must not hollow out the bead of another chain it
// meets). Junction cells are not built yet, so what the balls take from each
// other at a meeting point is not put back. Returns an empty manifold when
// nothing could be built.
manifold::Manifold buildRoundSolid(const MergedMesh& m,
                                   const std::map<EdgeKey, std::vector<int>>& adj,
                                   const std::vector<Chain>& chains, double r, bool concave,
                                   int arcSegments);

// Build a colored debug solid: a thin box marker straddling each real edge,
// colored by class — concave feature (red), convex feature (green), rejected
// tessellation seam (grey). Coplanar triangulation diagonals (near-zero
// dihedral) are omitted; they are not edges of the shape. Marker thickness is a
// fraction of the mesh bounding-box diagonal so it reads at any scale. Returns
// nullptr if there are no edges to draw.
std::unique_ptr<PolySet> debugEdgeMarkers(
  const MergedMesh& m, const std::map<EdgeKey, std::vector<int>>& adj, double thresholdDeg);

// Build a colored debug solid for the computed spine frames: the ball center C,
// both tangency points TA/TB, and connector legs from each vertex to its
// tangency points. Returns nullptr if there are no valid frames.
std::unique_ptr<PolySet> debugSpineMarkers(const MergedMesh& m,
                                           const std::vector<SpineFrame>& frames);

}  // namespace fillet::detail
