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
// Ties are rejected, not accepted. An exact tie is reachable — min_angle= can
// name any value, including one a surface's own facets turn by — and there the
// dihedrals of a tessellated surface land a few ulp either side of the
// threshold, splitting edges that are identical by symmetry. Rejecting keeps a
// prism a prism; accepting would round every facet of one. The margin is four
// orders above that ulp noise and far below any angle a caller chose on purpose.
// The default threshold is deliberately kept off every common facet angle so
// that no unnamed threshold ever lands here.
inline bool isFeatureAngle(double dihedralDeg, double thresholdDeg)
{
  return dihedralDeg >= thresholdDeg * (1 + 1e-9);
}

// The dihedral above which an edge is a feature rather than a curve-tessellation
// seam, when min_angle= names no other value. A constant, not a quantity read
// off the mesh or off $fn/$fa: classification is then a property of the solid
// alone, identical whether the solid arrived at stock defaults, at an explicit
// $fn, or through an STL round trip.
//
// 48 clears every tessellation seam a bench model produces (the widest measured
// is 36 degrees, on a 10 mm cylinder at stock defaults) while sitting below the
// steep part of every real intersection curve, and it leaves the 45-degree wall
// seams of a cylinder($fn=8) classified as seams.
//
// It sits off every facet angle a low $fn produces — 51.43 at 7, 45 at 8, 40 at
// 9, 36 at 10 — by at least 3 degrees. A threshold equal to a facet angle would
// decide a whole prism's classification on where its computed dihedrals land
// relative to the tie margin, and measured facet angles are not exact: a
// sphere's latitude rings read about 0.065 degrees off nominal, and a resize()d
// mesh drifts further.
inline constexpr double kDefaultCreaseThresholdDeg = 48.0;

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
//
// A chain has two polylines, and the distinction only appears once one has been
// resampled. `raw` is the crease as the mesh has it, every vertex of it, and is
// never altered by anything here. The stations — the points the bead's sections
// are placed at — are `at`, `pts` and `stations` read together: `at[i]` is where
// station i sits in `raw`'s own parameter (integer k meaning exactly `raw[k]`,
// k + f meaning f of the way along raw segment k), `pts[i]` is its position, and
// `stations[i]` is the mesh vertex it IS, or -1 where it is a new point interior
// to a raw segment.
//
// The list is called `stations` and not `verts` because it is not the vertices
// of this crease. Reading it as if it were is a bug this builder has shipped
// four times — in `checkChainSizes`, `filletedEdges`, `chainJunctions` and a
// seam predicate since removed — each time found at integration and never by a
// test. It is
// private for the same reason: the only two questions it can answer from outside
// are `stationCount()` and `openEnds()`. Anything else about the crease is a
// question for `rawRun()`, `param()`, `point()`, `inEdge()`, `outEdge()` or
// `rawMid()`.
//
// `at` and `pts` empty is the identity: one station per crease vertex, which is
// how every chain leaves buildChains and how it stays unless resampleChains has
// something to fix. Every accessor below reduces to the plain expression it
// replaced in that case, so an unresampled chain is built from bit-identical
// inputs.
//
// An OPEN chain's two ends are exact mesh vertices, resampled or not. Chains are
// cut at every vertex of crease-degree other than two, so a junction is a chain
// end by construction, and the whole of the junction, brush-coverage and
// corner-cell bookkeeping identifies ends by their mesh vertex.
//
// A CLOSED chain has one pinned station and it is station 0. A ring has no last
// station to pin — station count-1 is placed by arc length like any other — so
// the last station of a resampled ring IS -1, and station 0 is the only one of
// it that can be read as a mesh vertex. That is why the end vertices are handed
// out by `openEnds()`, which answers false on a ring and writes nothing: the
// question and the guard that makes it legitimate are one call, so no caller can
// take an end vertex without having handled the ring case. A guard that a caller
// could forget, or that a build could compile out, would not do — every consumer
// that reaches for the last station has to skip rings, and this is what makes it
// so in every build rather than by convention.
//
// `stationCount()` equals `rawCount()` today, because the resampler emits as
// many stations as the crease has segments. Nothing enforces that; the invariant
// is pinned by a test rather than by the type. If the station count ever stops
// matching, every `stationCount()` here silently changes meaning, so read
// `rawCount()` when the question is about the crease.
struct Chain
{
  bool closed = false;
  std::vector<SpineInterval> keep;
  std::vector<int> raw;
  std::vector<double> at;
  std::vector<Vector3d> pts;

  // How many stations the bead's sections are placed at. In station space, which
  // is what `keep`, `at`, `pts` and the section lists are all in. Not a count of
  // anything the mesh has — that is `rawCount()`.
  int stationCount() const { return static_cast<int>(stations.size()); }

  // The two mesh vertices an OPEN chain ends on. False, with neither output
  // touched, where there is no such pair: a ring, whose last station is placed by
  // arc length like any interior one and is -1 on any chain the resampler
  // touched, and a chain of fewer than two stations, which has no two ends. The
  // guard is the return value and not an assertion, so it is there in a build
  // with NDEBUG set as much as in one without; and it is the only way to reach a
  // station's mesh vertex from outside the struct, so the -1 cannot be read as
  // one by writing the check differently or by leaving it out.
  [[nodiscard]] bool openEnds(int& front, int& back) const
  {
    if (closed || stations.size() < 2) return false;
    front = stations.front();
    back = stations.back();
    return true;
  }

  // Replace the station list. The crease walk, the resampler and the raw-station
  // rebuild are the only three things that do this.
  void setStations(std::vector<int> s) { stations = std::move(s); }

  // The crease polyline, which is `raw` once anything has set it and the station
  // list before that — buildChains fills `raw`, so the fallback is only for a
  // Chain assembled by hand, as the unit tests do.
  const std::vector<int>& rawRun() const { return raw.empty() ? stations : raw; }
  int rawCount() const { return static_cast<int>(rawRun().size()); }

  // Where station i sits in the crease's own parameter.
  double param(int i) const { return at.empty() ? static_cast<double>(i) : at[i]; }

  // Station i's position. Identical to m.pos[stations[i]] whenever the station
  // is a mesh vertex, since `pts` is filled by copy.
  const Vector3d& point(const std::vector<Vector3d>& pos, int i) const
  {
    return pts.empty() ? pos[stations[i]] : pts[i];
  }

  // The mesh edge the crease arrives at station i on, and the one it leaves by.
  // At a station that is a mesh vertex these are the two crease edges meeting
  // there, exactly as they always were; at an interpolated station both are the
  // one raw segment it lies inside, which is what says such a station cannot be
  // a corner of the crease. {-1, -1} where the crease ends.
  std::pair<int, int> inEdge(int i) const
  {
    const std::vector<int>& run = rawRun();
    const int n = static_cast<int>(run.size());
    const double p = param(i);
    const int k = static_cast<int>(p);
    if (p > static_cast<double>(k)) return {run[k], run[(k + 1) % n]};
    if (k > 0) return {run[k - 1], run[k]};
    return closed && n > 1 ? std::pair<int, int>{run[n - 1], run[0]}
                           : std::pair<int, int>{-1, -1};
  }
  std::pair<int, int> outEdge(int i) const
  {
    const std::vector<int>& run = rawRun();
    const int n = static_cast<int>(run.size());
    const double p = param(i);
    const int k = static_cast<int>(p);
    if (p > static_cast<double>(k)) return {run[k], run[(k + 1) % n]};
    if (k + 1 < n) return {run[k], run[k + 1]};
    return closed && n > 1 ? std::pair<int, int>{run[n - 1], run[0]}
                           : std::pair<int, int>{-1, -1};
  }

  // A mesh edge representative of the crease between stations i and j: the raw
  // segment their midpoint falls in, so a station segment spanning several raw
  // ones is asked about in the middle rather than at either end.
  std::pair<int, int> rawMid(int i, int j) const
  {
    const std::vector<int>& run = rawRun();
    const int n = static_cast<int>(run.size());
    const double a = param(i);
    double b = param(j);
    if (b <= a) b += static_cast<double>(n);  // the closing segment of a ring
    const int k = std::min(static_cast<int>(0.5 * (a + b)), n - 1);
    return {run[k % n], run[(k + 1) % n]};
  }

private:
  // The mesh vertex under each station, or -1 where the station is interior to a
  // raw segment. Private: see the note above the struct. Everything this can
  // legitimately be asked is above it.
  std::vector<int> stations;
};

// Walk the selected edges into chains via shared vertices. Vertices of degree 2
// are interior stations; degree 1 are open ends; degree >= 3 are branch/junction
// vertices where chains terminate. Returns chains in canonical order.
std::vector<Chain> buildChains(const MergedMesh& m, const std::vector<EdgeKey>& edges);

// Put a chain's stations back at even spacing, before anything is built from it.
//
// Where two tessellations cross, the crease they share has wildly unequal
// segments: on a hole through a curved wall the shortest is a couple of
// thousandths of the median, and that ratio gets worse with refinement rather
// than better. One station per crease vertex then puts two stations almost on
// top of each other; their averaged wall normals are ill-conditioned, the arcs
// they carry sit at different depths and cross, and the wedge left between them
// stands in the bore as a fin.
//
// The fix is spacing, and spacing alone: the stations are laid out at equal arc
// length along the crease, as many of them as the crease has segments. Density
// is therefore exactly what it was — this buys regularity without paying for it
// in sampling, which is what dropping vertices instead would do, and what would
// show up as a bead lofting longer chords and dipping further off the true wall.
//
// A station that lands between two crease vertices is a new point *on* the
// crease polyline, so the crease is not moved, only re-divided; its walls are
// the walls of the raw segment it lies in. The two ends are pinned to their mesh
// vertices, since they are where corner cells are built and where the brush's
// coverage is tested, and a ring's canonical first vertex is pinned with them.
void resampleChains(const MergedMesh& m, std::vector<Chain>& chains, double fraction);

// What counts as a sliver, and so whether a chain is resampled at all: a segment
// shorter than half its chain's median. Strictly below 1 is the whole point — a
// crease whose segments are all near its median has none, is never resampled,
// and comes out of the builder bit for bit as it went in. A hole in a flat plate
// and a crease along a surface of revolution's axis are both in that case.
inline constexpr double kSliverFraction = 0.5;

// Which stretches of a chain the brushes select. The spine is intersected, not
// the tool volume: each segment is cast against the brush and the crossings
// become interval ends in chain-parameter space, so a brush boundary landing on
// a station is a parameter near 0 or 1 rather than a coin flip about whether
// that station is in.
//
// Intervals the brush cut and left shorter than `debounce` (a length along the
// spine) are discarded. A brush face nearly tangent to the spine crosses it
// twice a hair apart, and the stub of bead that would come of it is never what
// was meant. A stretch the brush did not cut at either end is exempt: the whole
// crease was selected, nothing is being clipped, and how long the crease happens
// to be is not the brush's business — a brush containing the entire model has to
// be a no-op however short the creases in it are.
//
// Returns the intervals covering the whole chain when the brush contains all of
// it, and nothing at all when it contains none — the caller distinguishes the
// two, since an empty `Chain::keep` means the opposite of an empty return here.
std::vector<SpineInterval> chainSelection(const MergedMesh& m, const Chain& chain,
                                          const BrushVolume& brush, double debounce);

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
  int triA = -1, triB = -1;  // one triangle of each wall, as StationNormals gives them
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
  Crowded,  // a neighbouring crease sits inside the material this one needs
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
// radius). And it has to have the room it needs: another crease's contact point
// inside the corner this blend occupies — between the two tangency lines, which
// is the material the bead is made of and not the whole of the seated ball — is
// a crease whose bead this one would eat, and neither can be built as asked.
// Two beads sharing a face therefore fit until their tangency lines meet, which
// on a cube is a radius of half the side.
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
                                        const Chain& chain, double t, bool concave,
                                        double thresholdDeg);

// Build the chamfer/bevel tool solid: hull each consecutive pair of wedge
// sections into one cell, then union the cells. Chains meeting at a vertex
// simply overlap there, which is what a chamfered corner is — the union of the
// planar cuts — so these tools need no junction cell of their own. Returns an
// empty manifold when nothing could be built.
manifold::Manifold buildWedgeSolid(const MergedMesh& m,
                                   const std::map<EdgeKey, std::vector<int>>& adj,
                                   const std::vector<Chain>& chains, double t, bool concave,
                                   double thresholdDeg);

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
  double eps = 0.0;  // how far past its walls this section stands
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
                                        int arcSegments, double thresholdDeg);

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

// Find the junctions among a chain set: vertices two or more open-chain ends
// land on. A centre is kept only if it clears every wall it was not solved
// against — a solution tangent to its own three walls but buried in a fourth
// would gouge the fillet back from that fourth wall, so it is discarded rather
// than clamped. Near-singular solves and centres absurdly far from the vertex
// are rejected too, since both produce coordinates a hull will either choke on
// or blow up around; a junction with no centre left simply gets no corner.
//
// `noCorner` are the vertices a brush arrived at without covering, as
// dropUncoveredCorners marks them. Two chain ends at a vertex is enough to build
// one — a crease left out of the selection does not stop the two that were kept
// from meeting there — but a corner the brush was cut short of is one the caller
// asked not to have, and the two rules have to be the same rule.
std::vector<Junction> chainJunctions(const MergedMesh& m,
                                     const std::map<EdgeKey, std::vector<int>>& adj,
                                     const std::vector<Chain>& chains, double r, bool concave,
                                     const std::set<int>& noCorner);

// Drop the selections that arrive at a corner without covering it. A corner is a
// vertex three or more chain ends land on in `candidates`, the selection as it
// stood before any brush touched it; it is covered when every one of those ends
// is selected for `r` of crease back from it in `chains`, which is the stretch
// the corner cell occupies. Short of that no cell is built, and a bead that stops
// inside the stretch one would have filled is a stub meeting nothing at a sharp
// vertex — so the stretch is dropped too, and what the brush asked for at that
// corner is answered with nothing rather than with half of it.
//
// The valence has to come from `candidates` rather than from `chains`, because a
// brush can remove an arm from `chains` altogether — by covering less of it than
// the debounce keeps, or by covering only its far end — and an arm that is not
// there is indistinguishable from one the model never had. Counting the arms
// that survived is what made a corner appear as the brush shrank.
//
// Returns only the corners where *nothing* was covered, which are the ones a
// brush was drawn around and gets nothing at. A corner some crease through it
// does cover is the neighbour-clipping case that makes "this edge and no other"
// expressible, and is silent on purpose.
//
// A chain left with none of its selection is removed from `chains` outright,
// since an empty `Chain::keep` reads as the whole chain. Selections that reach a
// chain end no corner stands at are untouched: nothing is being closed there, so
// there is no stretch a cell has a claim on.
//
// `uncovered`, when given, receives every corner that could not be had, which is
// more than the return value: the return is only the ones where nothing at all
// was covered and the caller has something to say. chainJunctions needs the
// whole set, since a corner cell at any of them is material the brush excluded.
std::vector<int> dropUncoveredCorners(const MergedMesh& m, std::vector<Chain>& chains,
                                      const std::vector<Chain>& candidates, double r,
                                      std::set<int> *uncoveredOut = nullptr);

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
                                   int arcSegments, double thresholdDeg,
                                   const std::set<int>& noCorner);

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
