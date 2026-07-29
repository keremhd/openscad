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

#include "geometry/fillet/FilletBrush.h"
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

// Whether an edge turning dihedralDeg counts as a feature rather than as a seam
// the tessellation itself produced. Every caller must go through this: the
// classification, the selection, the smooth-surface grouping and the debug
// colouring all ask the same question, and at a value where they disagreed an
// edge would be a crease to one and a flat seam to another.
//
// Ties are rejected, not accepted. An exact tie is reachable — the threshold is
// 1.5x the caller's facet angle, so a model tessellated at two thirds the
// caller's $fn turns by exactly it — and there the dihedrals land a few ulp
// either side of the threshold, splitting edges that are identical by symmetry.
// Rejecting keeps a prism a prism; accepting would round every facet of one.
// The margin is four orders above that ulp noise and far below any angle a
// caller chose on purpose.
inline bool isFeatureAngle(double dihedralDeg, double thresholdDeg)
{
  return dihedralDeg >= thresholdDeg * (1 + 1e-9);
}

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

// A stretch of one chain, in chain-parameter space: station i sits at parameter
// i, so a value between two integers names a point partway along that segment.
// A brush boundary is recorded as that parameter rather than as a nearest
// station, which is what makes the blend end at a fixed physical point instead
// of moving when the target's tessellation changes.
using SpineInterval = std::pair<double, double>;

// A spine: an ordered run of merged vertex indices along one crease. `closed`
// marks a ring (the hole-mouth case), where the last vertex reconnects to the
// first. Ordering is canonical (derived from vertex positions, not mesh
// traversal order) so downstream indices stay put under a small parameter nudge.
//
// `keep` is the part of the chain the selection brushes picked out, empty
// meaning the whole of it — the common case, and the only one when no brush was
// given at all. A chain the brushes miss entirely is not carried with an empty
// `keep`; it is dropped from the chain list.
struct Chain
{
  std::vector<int> verts;
  bool closed = false;
  std::vector<SpineInterval> keep;
};

// Walk the selected edges into chains via shared vertices. Vertices of degree 2
// are interior stations; degree 1 are open ends; degree >= 3 are branch/junction
// vertices where chains terminate. Returns chains in canonical order.
std::vector<Chain> buildChains(const MergedMesh& m, const std::vector<EdgeKey>& edges);

// Which stretches of a chain the brushes select. The spine is intersected, not
// the tool volume: each segment is cast against the brush and the crossings
// become interval ends in chain-parameter space, so a brush boundary landing on
// a station is a parameter near 0 or 1 rather than a coin flip about whether
// that station is in.
//
// Intervals shorter than `minLength` (a length along the spine) are discarded. A
// brush face nearly tangent to the spine crosses it twice a hair apart, and the
// stub of bead that would come of it is never what was meant.
//
// Returns the intervals covering the whole chain when the brush contains all of
// it, and nothing at all when it contains none — the caller distinguishes the
// two, since an empty `Chain::keep` means the opposite of an empty return here.
std::vector<SpineInterval> chainSelection(const MergedMesh& m, const Chain& chain,
                                          const BrushVolume& brush, double minLength);

// The two wall normals at one chain station, averaged over the station's
// incident chain edges. `triA`/`triB` name one triangle of each wall, which is
// the handle onto the surface that wall belongs to. `valid` is false where the
// station has no usable pair (a dangling edge, a degenerate triangle).
struct StationNormals
{
  Vector3d v;
  Vector3d nA, nB;
  int triA = -1, triB = -1;
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

// Group the triangles into surfaces: two triangles belong to the same surface
// when the edge between them is a seam rather than a crease, i.e. when its
// dihedral falls below the same threshold that separates feature edges from
// tessellation. Returns one surface id per triangle. A bore's facets come out as
// one surface, a cube's six faces as six.
std::vector<int> smoothSurfaces(const MergedMesh& m,
                                const std::map<EdgeKey, std::vector<int>>& adj,
                                double thresholdDeg);

// Where the tool meets the model at one chain station, and the ball seated in
// the crease there. For the rounded tools that ball is the one being rolled; for
// the wedge tools it is the ball whose tangency points are the setback points,
// which is the same object for the purpose of asking how much room the tool
// needs. `surfaceA`/`surfaceB` are the surfaces the two contact points must land
// on for the tool to meet the model tangentially at all.
//
// `TA`/`TB` are the points of those surfaces nearest the ball centre, so they
// lie on the model rather than being stepped off C along a wall normal — which
// would assume the wall flat and miss a curved one by its sagitta. `offFace` is
// non-zero where the nearest point has run onto the boundary of its surface,
// which is the ball hanging off the end of a wall it is meant to meet.
struct ChainContact
{
  Vector3d v;
  Vector3d C;
  Vector3d TA, TB;
  double radius = 0.0;
  int surfaceA = -1, surfaceB = -1;
  int vert = -1;        // the mesh vertex, or -1 between two of them
  double offFace = 0.0; // how far past the end of its wall the blend would stop
  bool valid = false;
};

// The contact points and seated ball along a chain. `wedge` selects the
// chamfer/bevel reading of `size` (a setback taken directly) over the rounded
// one (a radius, whose setback is r*tan(phi/2)).
//
// Only the stretches of the chain named by `Chain::keep` are answered for: a
// point the brushes left out carries no bead, so nothing about the model there
// can refuse a size nobody asked to build. Those points come back in place, as
// invalid contacts, so the list still runs from one end of the chain to the
// other.
//
// `samplesPerSegment` is the FEWEST points to add between two stations, with the
// walls interpolated; a segment long against the size gets more, one sample per
// size along it. A crease is only ever sampled where the mesh has a vertex, and
// on a tapering feature — a spike, a wedge running to nothing — the room
// available between two stations can fall below what the tool needs without
// either station noticing, so how finely the segment is walked has to follow the
// size being asked about rather than the mesh.
std::vector<ChainContact> chainContacts(const MergedMesh& m,
                                        const std::map<EdgeKey, std::vector<int>>& adj,
                                        const Chain& chain, double size, bool concave, bool wedge,
                                        const std::vector<int>& surfaceOf,
                                        int samplesPerSegment = 0);

// Distance from a point to a closed triangle — face, edge or corner, whichever
// is nearest. What the size gate asks with it is whether a contact point still
// lands on the wall it is meant to touch.
double pointTriangleDistance(const Vector3d& p, const Vector3d& a, const Vector3d& b,
                             const Vector3d& c);

// Why a chain cannot carry the size it was asked for.
//
// A size that does not fit is refused, not clamped: clamping one crease forces
// the creases it meets to agree, and following that to its fixed point runs a
// min over the whole connected network, so one tight corner would silently
// resize a fillet on the far side of the part.
enum class SizeFault
{
  Fits,
  OffFace,  // the contact line runs off the end of the wall it should meet
  Crowded,  // a neighbouring crease sits inside the space this one needs
};

// One chain's verdict, carrying where it was decided and by how much, so the
// warning can say what is wrong rather than only that something is.
struct SizeVerdict
{
  SizeFault fault = SizeFault::Fits;
  Vector3d where = Vector3d::Zero();
  double amount = 0.0;
};

// Check every chain against the size asked for, in two ways.
//
// The tool has to *touch* the model: a seated ball whose nearest point on a wall
// has run onto that wall's boundary is hanging off the end of it, and no
// size-preserving blend exists there at all (the arms of an L shorter than the
// radius). And it has to have the room it needs: a crease that
// is not this one, sitting closer to the seated ball than the ball's own radius,
// is a crease whose bead this one would eat — the two features are competing for
// the same material, and neither can be built as asked.
//
// Creases that meet at a junction are exempt from the second test; that is what
// junction cells are for. Both tests are answered per chain, and a chain that
// fails is dropped whole.
std::vector<SizeVerdict> checkChainSizes(const MergedMesh& m,
                                         const std::map<EdgeKey, std::vector<int>>& adj,
                                         const std::vector<Chain>& chains, double size,
                                         bool concave, bool wedge, double thresholdDeg);

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
// sections into one cell, then union the cells. Chains meeting at a vertex
// simply overlap there, which is what a chamfered corner is — the union of the
// planar cuts — so these tools need no junction cell of their own. Returns an
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
//
// `v` is the crease point the section was taken at, and `C` the arc's centre;
// between them and `w[0]`, `w[1]` — the two tangency points — the section knows
// its own frame, which is what lets a corner cell rebuild the profile at its own
// distance past the walls instead of reusing this one.
struct RoundSection
{
  std::array<Vector3d, 5> w;
  std::vector<Vector3d> u;
  Vector3d v = Vector3d::Zero();
  Vector3d C = Vector3d::Zero();
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

// A vertex where three or more creases meet. `faceNormals` are the distinct
// outward normals of every wall that touches the vertex — not just the walls of
// the creases this tool selected, since a face arriving on an edge of the other
// sign is no less solid — and `ballCentres` are the extreme positions the rolling
// ball can occupy there.
//
// The legal positions near the vertex are the intersection of the walls' own
// half-spaces pushed in by r, a convex region whose corners are exactly those
// centres. Three walls pin down one corner, and every spine running into the
// vertex stops at it simultaneously, which is what lets the corner close
// exactly. More than three walls generally give several, because pushing them in
// by r breaks up the single point the originals shared.
struct Junction
{
  int vert = -1;
  std::vector<Vector3d> faceNormals;
  std::vector<Vector3d> ballCentres;
};

// Find the junctions among a chain set: vertices where three or more open-chain
// ends land. A centre is kept only if it clears every wall it was not solved
// against — a solution tangent to its own three walls but buried in a fourth
// would gouge the fillet back from that fourth wall, so it is discarded rather
// than clamped. Near-singular solves and centres absurdly far from the vertex
// are rejected too, since both produce coordinates a hull will either choke on
// or blow up around; a junction with no centre left simply gets no corner.
std::vector<Junction> chainJunctions(const MergedMesh& m,
                                     const std::map<EdgeKey, std::vector<int>>& adj,
                                     const std::vector<Chain>& chains, double r, bool concave);

// Build the fillet/round tool solid: the wedge W hulled from consecutive
// sections along every chain, plus a corner cell at each junction, minus the
// canal U hulled the same way, plus a ball at each corner. Spines are truncated
// where the rolling ball would first cut into a wall it is not riding, so U is
// exactly the set of positions the ball can occupy and the subtraction is a
// single global one: the corner ball has to reach the wedges of every chain that
// meets there. Returns an empty manifold when nothing could be built.
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
