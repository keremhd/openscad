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

#include "geometry/fillet/FilletBlend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include <manifold/manifold.h>

#include "core/FilletNode.h"
#include "geometry/Geometry.h"
#include "geometry/PolySet.h"
#include "geometry/PolySetBuilder.h"
#include "geometry/fillet/FilletBrush.h"
#include "geometry/fillet/FilletMesh_internal.h"
#include "geometry/linalg.h"
#include "geometry/manifold/ManifoldGeometry.h"
#include "utils/printutils.h"

using namespace fillet::detail;

namespace {

// ---------------------------------------------------------------------------
// Tunable knobs for the blend's crease walk, named here in one place rather than
// left as literals buried in each function. Two companions live with the
// classification core in the internal header: the feature/crease threshold
// (min_angle, defaulting to kDefaultCreaseThresholdDeg) and the smooth-surface
// grouping threshold (kDefaultSurfaceThresholdDeg 10 deg). The three below are
// specific to this file — the "same 40 deg" the selection walk and the pass-
// through seating both rely on is then tuned once, and the along-sweep station
// spacing sits beside them.
// ---------------------------------------------------------------------------

// A crease continues into the neighbour whose direction turns least; a turn past
// this bound is a different crease branching off (a facet seam meeting a rim at
// a right angle, a crease dying into a corner), not the same one continuing.
inline constexpr double kCreaseFollowMaxTurnDeg = 40.0;

// How near to a straight line two edges at a vertex must run for the second to be
// the first one CONTINUING rather than a crease turning off it. The follow walk
// uses this to recognise a tessellation seam grazing the crease and refuse it (see
// straightThrough). Raise it and a seam that leaves at a slight angle reads as
// straight and the walk stops following genuine creases through their own
// vertices; lower it and the walk runs off down cylinder seams at tangent
// junctions.
inline constexpr double kCreaseStraightDeg = 20.0;

// A crease passes THROUGH a vertex — rather than turning a corner or branching a
// junction — when its two edges leave nearly opposite, within this bound. It is
// the same angle the follow walk uses, and the shared cross-section that welds
// the two strips at such a vertex depends on the two staying in step.
inline constexpr double kPassThroughMaxTurnDeg = 40.0;

// Mitre limit: how far a set-back vertex may stand off its own position, in
// setbacks. A face-sector corner's mitre stands 1/sin(half-angle) setbacks out,
// so this is a floor on the sector angle -- 4 is 29 deg, tighter than the 40 deg
// turn above, at which two edges stop being one crease and become a corner at
// all. Anything that sharp is a razor sector a tessellation put there, not a
// corner, and its mitre is off the face rather than on it.
inline constexpr double kMitreLimit = 4.0;

// Along-sweep station floor: a long selected crease is split so a straight fillet
// holds a constant profile instead of tapering to its corner-distorted ends. The
// cap on the spacing is kAlongSweep * size — an along-sweep counterpart to the
// arc discretizer's along-arc segment count, named here beside the angle caps
// rather than buried at the one call site.
inline constexpr double kAlongSweep = 4.0;

// Mixed-corner pull-in station, as the interpolation fraction from the shallowest
// to the deepest face mitre. A mixed corner seats both strip feet at one
// perpendicular station along the edge to un-twist the blade; the deepest mitre
// (fraction 1.0) is the smallest station that keeps both feet clear of the
// neighbouring concave setback under a purely planar analysis, but it retreats the
// convex round-overs a full radius and leaves a necked notch. The concave fillet
// recedes in 3D toward the corner, so the bead can seat closer: this fraction pulls
// the station in from the deepest mitre so the round-overs flow into the corner. A
// symmetric corner (the concave crease itself) keeps its mitre unchanged. The
// driver tries this first and falls back to the full mitre (1.0), then the
// baseline, if the tighter seat cannot weld.
inline constexpr double kPullStationFrac = 0.7;

// How far a corner cap's ring must stand off the pole ray for the polar fan to have
// any width there. A degree is well under the angle any real ring point subtends at
// the cap centre (the coarsest cap layers turn several degrees) and well over the
// ulp noise in a direction read off a millimetre-scale offset point.
inline constexpr double kCapPoleClearDeg = 1.0;

// --- station clamps --------------------------------------------------------

// The along-sweep clamp on a pull-in station, as a fraction of the edge's length:
// a corner may never seat its cross-section further than this along its own edge,
// so a short corner-to-corner edge keeps a straight middle between its two ends.
// Push it toward 0.5 and the two ends meet and the strip has no middle left;
// pull it down and a crowded corner seats short of the mitre it needs.
inline constexpr double kStationLenFrac = 0.45;

// The same guard for a corner-station OVERRIDE, which is exempt from the clamp
// above (it is the exact seat the corner's own surface is built on) but must still
// stop short of whatever the far end of the edge takes: this fraction of the room
// the far station leaves. At 1.0 the two seats touch and the strip folds; well
// below it the strip seats short of the corner surface and the corner refuses.
inline constexpr double kStationFarClearFrac = 0.9;

// --- offset-fold repair ----------------------------------------------------

// A collapsed fold's survivor must be a real seat, not a near-parallel mitre that
// raced off down the boundary: nothing further than this many setbacks from its
// own vertex is accepted. Raise it and a runaway crossing teleports a strip across
// the solid; lower it and a legitimately deep mitre stops trimming its spike.
inline constexpr double kFoldSurvivorSetbacks = 2.0;

// How many times each repair sweeps its whole edge set: one collapse can expose
// the next, and one flip can expose the next. Both converge in far fewer than this
// on the bench; the cap is only there so a pathological mesh cannot loop.
inline constexpr int kFoldRepairSweeps = 8;
inline constexpr int kInsetFlipPasses = 8;

// --- corner-roll reconstruction --------------------------------------------

// How tightly a ring arc's reconstructed rolling-ball centres must cluster for the
// arc to be read as one cross-section of one roll, as a fraction of the blend size.
// Widen it and a clamped or twisted arc is mistaken for a roll and the corner is
// drawn onto a surface that is not there; tighten it and a legitimately coarse arc
// is refused and the corner keeps its plain lofted field.
inline constexpr double kRollArcScatterFrac = 0.15;

// How far off the exact two-radii tube distance the concave section's own centre
// may sit and still be accepted as a ball of the transition family, as a fraction
// of the blend size. This is the gate on "is this the canonical corner at all".
inline constexpr double kRollTubeTolFrac = 0.05;

// The march along the transition's centre curve: step length as a fraction of the
// blend size, and the cap on the number of steps. A longer step overshoots the
// pinch the march must stop at; a shorter one wastes work and the cap then ends
// the chain before the pinch, leaving the corner short of the face.
inline constexpr double kRollMarchStepFrac = 0.12;
inline constexpr int kRollMarchMaxSteps = 256;

// Newton iterations pulling each marched centre back onto both tubes. It converges
// quadratically from a step this short, so this is a cap and not a schedule.
inline constexpr int kRollNewtonIters = 8;

// Drawing the lofted field onto the reconstructed rolls: how many relax-and-redraw
// rounds, how far each relaxation moves a sample toward its four neighbours, and
// the pull guard's two terms (a sample moves onto the roll only if the projection
// is nearer than (base + span * weight) * size). Fewer rounds or a smaller rate
// leave the field bunched and pleated where the merge fans rows off one point;
// a looser guard lets a rollProject that picked the wrong side of a degenerate
// configuration drag the patch off the weld it has to reach.
inline constexpr int kRollRelaxPasses = 48;
inline constexpr double kRollRelaxRate = 0.85;
inline constexpr double kRollPullGuardBase = 0.25;
inline constexpr double kRollPullGuardSpan = 1.25;

// --- exact-corner fit ------------------------------------------------------

// How exactly the ring must match the tube the corner hands over to, as a fraction
// of the blend size — the tolerance on every check in emitCornerTube/cornerCloses.
// This is a fit gate, not a shape knob: loosen it and a ring that is NOT the
// canonical corner passes and the emitted tube tears away from the strips; tighten
// it past the arc construction's own rounding and no corner ever fits.
inline constexpr double kTubeFitTolFrac = 1e-3;

// The two round-overs (or the swing's two ends) must genuinely turn apart rather
// than lie back-to-back on one line: their directions' |cos| must stay under this.
// Nearer 1 and a degenerate near-straight ring is fitted as a corner.
inline constexpr double kMinSwingTurnCos = 0.95;

// --- miscellaneous ---------------------------------------------------------

// The largest number of pieces one long crease edge may be split into by the
// along-sweep floor. A backstop against a pathological length/size ratio, not a
// resolution choice: it only binds when the edge is 256 caps long.
inline constexpr int kMaxEdgeSplitParts = 256;

// The pole-free saddle's control-arm length, as a fraction of the chord from a
// boundary point to the patch centre. It sets how far the radial curve bulges
// before turning in. A full-chord arm carries the curve clear across the patch and
// stands proud of the opposite-sign valley as a needle flap; too short and the
// patch dives straight for the centre and loses tangency with the strips.
inline constexpr double kSaddleArmFrac = 0.55;

// Two face normals count as the same direction (a duplicate, for the corner ball's
// normal set) above this cosine, and three normals are independent enough to pin a
// centre when their pairwise cross / triple product clears this floor. Loosen
// either and a near-degenerate triple inverts into a wild ball centre.
inline constexpr double kNormalDuplicateCos = 0.999;
inline constexpr double kBasisIndependenceMin = 0.1;

// Two triangles are the SAME flat face — the one face both of a corner's
// round-overs run along — only this close to parallel. It gates the exact-corner
// reseat, so a looser value would reseat strips against a face that is really two,
// and the corner they hand over to would not close.
inline constexpr double kSharedFaceCos = 0.999999;

// --- corner-patch sampling -------------------------------------------------

// The Coons saddle's tangent control arm, as a fraction of the chord to the patch
// centre — the same role kSaddleArmFrac plays for the concentric saddle, but for
// the cross curves that run concave boundary to convex boundary. Longer and the
// cross curve overshoots into the opposite-sign side; shorter and it leaves the
// boundary off the fillet tangent and the patch creases against the strips.
inline constexpr double kCoonsArmFrac = 0.42;

// Interior sampling floor for a corner whose real rolls are known: at least this
// many layers across a cross curve, and enough inserted rows to match. At stock
// settings a small radius earns only a handful of fragments, and a field with
// nothing to move cannot shape anything — the corner keeps the coarse pinwheel the
// raw loft folds into. Only the INTERIOR densifies, so the ring keeps its exact
// vertices either way, and the floor goes inactive as soon as the tessellation
// earns more than it. Raising it costs triangles on every such corner.
inline constexpr int kCornerInteriorFloor = 16;

// The outer fraction of a cross curve held flush to the strip it leaves, at each
// end. Wider and the flush clamp reaches the middle of the patch and flattens the
// saddle; narrower and the patch bulges proud of the strip right at the seam.
inline constexpr double kCoonsFlushBand = 0.35;

// How far the roll-projection pull is ramped in from the patch's edges: in rows
// from either end, and in the cross-curve parameter from either boundary layer.
// The ramp is what stops a row dropping onto the roll while the connector chord it
// fans onto — which carries no interior layer and cannot follow — stays put.
inline constexpr double kRollPullRowMargin = 3.0;
inline constexpr double kRollPullSMargin = 0.15;

// The concentric saddle's sliver weld, as a fraction of the blend size: interior
// columns nearer than this to each other collapse together. Larger and the weld
// fuses columns that are not a collapsible neighbour pair, which the manifold
// check then rejects and the whole weld is dropped; smaller and the needle
// triangles it exists to remove survive.
inline constexpr double kSaddleWeldFrac = 0.06;

// The weld tolerance two cross-sections are compared at, in mm — the same distance
// OutMesh's positional weld quantises to, so two sections that pass here are
// exactly the sections OutMesh has already fused. Moving one without the other
// would let emitCorner skip a vertex the mesh did not actually sew.
inline constexpr double kWeldTolMm = 1e-6;

// --- face footprint --------------------------------------------------------

// The in-plane distance below which a footprint's own geometry is degenerate, as a
// fraction of the blend size: a link this short is no direction, a crossing this
// shallow is rounding, an ear this thin is not one. It is deliberately far coarser
// than the mesh weld -- it decides whether a construction is meaningful, not
// whether two points are the same point, and that second question is settled at
// the weld itself (kFootprintSeatTol below). Raise it and a real crossing is
// dismissed as rounding, leaving the spike it would have trimmed; lower it and the
// ear clip starts cutting slivers whose normals are noise.
inline constexpr double kFootprintEpsFrac = 1e-4;

// The dihedral above which two faces sharing an edge are a fold rather than a
// crease, in degrees. A fold proper is 180; a fold between two tessellated surfaces
// closes to within a few degrees of it rather than exactly, so the test is short of
// 180. Raise it past about 178 and the lapped strips this counts stop being counted;
// drop it below about 165 and honest creases on a coarse tessellation start being.
inline constexpr double kFoldDihedralDeg = 170.0;

// A blended crease edge shorter than this fraction of the blend size is a sliver
// the blend cannot resolve: its strip is one cross-section's worth of surface hung
// on an edge far shorter than the section is wide, so it blades and laps its
// neighbours. Its two ends are welded before anything is built. Raise it and real
// crease geometry is welded away -- a short but genuine segment of a chain stops
// being one, and the turn it carried lands on the wrong vertex; lower it and the
// sliver survives and its strip laps the strips either side of it.
inline constexpr double kSliverCreaseFrac = 0.05;

// Two consecutive points of a retreated boundary are ONE SEAT below this distance,
// in mm. They arrive coincident whenever a trim has collapsed a link away, and a
// zero-length link is an ear clipper's one unrecoverable input. It is the mesh's
// own weld and not a fraction of anything: below it the two are the same vertex to
// everything downstream, and above it they are two, and a strip welds to each.
// Raise it and a seat a strip still welds to is dropped from under it -- measured,
// at 1e-4 of the size it takes elbow_facet_endface's corner off the exact-torus
// branch by moving a boundary point the corner is fitted against.
inline constexpr double kFootprintSeatTol = kWeldTolMm;

// How near a straight line two selected edges at a vertex must run for the size
// gate to treat them as one crease cut into stations rather than two creases
// meeting. It is far tighter than any of the crease-walk angles above because it
// answers a different question: the along-sweep floor cuts a straight crease into
// pieces that are collinear to rounding, and refusing one piece while keeping the
// next leaves two parallel offset lines at different depths with no mitre between
// them. Raise it and a genuine corner is refused along with its neighbour; lower it
// past the rounding in a normalised direction and a station is left behind.
inline constexpr double kOverRoundRunDeg = 1.0;

// How many times the size gate may refuse and rebuild. Refusing only ever removes
// setback, so no refusal can create a new over-round face and the sweep converges;
// the cap is only there so a pathological mesh cannot loop. A build still over-round
// at the cap is emitted as it was before the gate existed — which is the failure
// mode this number guards: a cap short of where the refusal settles hands the last,
// ungated pass a selection the gate had already rejected, and the crumpled strips
// that gate exists to refuse are emitted after all. A tapering wall on a
// tessellated arc refuses one facet per pass as the handover walks along it, so the
// budget has to be a run of facets long, not a couple of corners.
inline constexpr int kOverRoundPasses = 12;

// How far a strip's seat may stand outside the outline of the face it is supposed
// to be tangent to, in blend sizes, before that crease is refused. Zero is the
// honest answer for a face that is one whole plane, but a tessellated wall is cut
// into a surface per facet, and a seat on one facet routinely spills a fraction of
// a millimetre onto the next — the same wall, a different surface id — which is not
// an escape at all. What is an escape is the mitre against a kept-sharp neighbour
// leaving the vertex the same way, which lands whole setbacks down the boundary and
// off the end of the face; measured, the two are 0.05 sizes and 1-3.6 sizes apart.
// One size is the physical line between them: a ball of the blend's own size seated
// at the vertex cannot touch anything further off the face than that, so nothing
// there is a seat. Lower it and a facet spill is refused as a race; raise it and the
// racing mitre is emitted and its strip comes back as a crumple.
inline constexpr double kSeatEscapeSizes = 1.0;

// How exactly a finished footprint triangulation must reproduce its own polygon's
// area, as a fraction of that area. It is the check that the hole bridges did not
// cross anything: a bridge that did produces a triangulation that laps over itself
// and misses by far more than any rounding. Loosen it and a folded bridge ships;
// tighten it past the ear clip's own rounding and every holed face falls back to
// the untrimmed emission.
inline constexpr double kFootprintAreaTol = 1e-6;

// ---------------------------------------------------------------------------
// Output mesh: a triangle soup with position-welded vertices, an orientation
// pass to make winding consistent, and a per-component volume-sign fix so the
// emitted normals point outward. Winding is therefore never the caller's
// concern — triangles are added in any order and the pass repairs them, exactly
// as a mesh library's fix_normals would.
// ---------------------------------------------------------------------------
struct OutMesh
{
  std::vector<Vector3d> V;
  std::vector<std::array<int, 3>> F;
  std::map<std::array<int64_t, 3>, int> weld;
  double q = 1e6;  // weld quantum: the reciprocal of kWeldTolMm, the stated weld tolerance

  int add(const Vector3d& p)
  {
    const std::array<int64_t, 3> key{static_cast<int64_t>(std::llround(p.x() * q)),
                                     static_cast<int64_t>(std::llround(p.y() * q)),
                                     static_cast<int64_t>(std::llround(p.z() * q))};
    auto [it, ins] = weld.try_emplace(key, static_cast<int>(V.size()));
    if (ins) V.push_back(p);
    return it->second;
  }

  void tri(int a, int b, int c)
  {
    if (a == b || b == c || a == c) return;  // degenerate after welding
    F.push_back({a, b, c});
  }
  void tri(const Vector3d& a, const Vector3d& b, const Vector3d& c) { tri(add(a), add(b), add(c)); }

  // --- the lap index --------------------------------------------------------
  //
  // foldPairs() is the verdict on a finished mesh: it needs consistent winding, so
  // it can only be asked once orient() has run and the whole blend is built. A
  // patch being chosen mid-build cannot wait for that, so the same defect is asked
  // for here in a winding-free form. Two triangles sharing an edge lap when they
  // are coplanar AND their two far corners fall on the SAME side of the shared
  // edge: the surface has doubled back. A flat pair puts them on opposite sides, a
  // crease puts them at an angle, and neither reads as a lap. Measured against the
  // same kFoldDihedralDeg as foldPairs, so a patch this accepts is one the census
  // accepts.
  std::map<std::pair<int, int>, std::vector<int>> lapIdx;

  static std::pair<int, int> ekey2(int a, int b) { return {std::min(a, b), std::max(a, b)}; }

  double faceArea(int f) const
  {
    return 0.5 * (V[F[f][1]] - V[F[f][0]]).cross(V[F[f][2]] - V[F[f][0]]).norm();
  }

  // Index every face not yet indexed, so later queries see them. Incremental, so
  // walking the corners costs one pass over the mesh in total rather than one per
  // corner.
  std::size_t lapIndexed = 0;
  void lapIndex()
  {
    for (; lapIndexed < F.size(); ++lapIndexed)
      for (int k = 0; k < 3; ++k)
        lapIdx[ekey2(F[lapIndexed][k], F[lapIndexed][(k + 1) % 3])]
          .push_back(static_cast<int>(lapIndexed));
  }

  // Do faces f and g lap along their shared edge (a,b)?
  bool lapsOn(int f, int g, int a, int b) const
  {
    if (faceArea(f) < kWeldTolMm * kWeldTolMm || faceArea(g) < kWeldTolMm * kWeldTolMm) return false;
    const Vector3d eh = (V[b] - V[a]);
    const double el = eh.norm();
    if (el < kWeldTolMm) return false;
    const Vector3d e = eh / el;
    auto perp = [&](int t) {
      for (int k = 0; k < 3; ++k) {
        const int x = F[t][k];
        if (x == a || x == b) continue;
        const Vector3d d = V[x] - V[a];
        return Vector3d(d - d.dot(e) * e);
      }
      return Vector3d(Vector3d::Zero());
    };
    const Vector3d pf = perp(f), pg = perp(g);
    const double lf = pf.norm(), lg = pg.norm();
    if (lf < kWeldTolMm || lg < kWeldTolMm) return false;
    // The interior angle between the two half-planes; -cos(kFoldDihedralDeg) is
    // cos of the small angle a fold leaves between them.
    return pf.dot(pg) / (lf * lg) > -std::cos(kFoldDihedralDeg * M_PI / 180.0);
  }

  // Lapped pairs involving the faces [from, F.size()): against each other and
  // against everything already in lapIdx. Each pair counted once.
  int lapsSince(std::size_t from) const
  {
    std::map<std::pair<int, int>, std::vector<int>> local;
    for (std::size_t f = from; f < F.size(); ++f)
      for (int k = 0; k < 3; ++k)
        local[ekey2(F[f][k], F[f][(k + 1) % 3])].push_back(static_cast<int>(f));
    int n = 0;
    for (const auto& [e, fs] : local) {
      std::vector<int> all = fs;
      if (const auto it = lapIdx.find(e); it != lapIdx.end())
        all.insert(all.end(), it->second.begin(), it->second.end());
      for (std::size_t i = 0; i < all.size(); ++i)
        for (std::size_t j = i + 1; j < all.size(); ++j) {
          if (all[i] < static_cast<int>(from) && all[j] < static_cast<int>(from)) continue;
          if (lapsOn(all[i], all[j], e.first, e.second)) ++n;
        }
    }
    return n;
  }

  // Drop the faces added since `n` (an abandoned candidate patch). Vertices added
  // alongside them stay: they are unreferenced, build() walks faces only, and the
  // weld map must keep its entries so a later candidate that lands on the same
  // point gets the same index.
  void dropFacesSince(std::size_t n) { F.resize(n); }

  // Fan-triangulate a ring (already ordered) from its centroid.
  void fan(const std::vector<int>& ring)
  {
    if (ring.size() < 3) return;
    Vector3d c = Vector3d::Zero();
    for (int i : ring) c += V[i];
    c /= static_cast<double>(ring.size());
    const int ci = add(c);
    const int n = static_cast<int>(ring.size());
    for (int i = 0; i < n; ++i) tri(ci, ring[i], ring[(i + 1) % n]);
  }

  // Make winding consistent per connected component, then orient each component
  // outward by its signed volume. Returns the number of boundary (once-used)
  // edges left over — nonzero means the surgery left a hole (an invalid solid).
  int orient()
  {
    const int nf = static_cast<int>(F.size());
    // undirected edge -> the (<=2) faces on it
    std::map<std::pair<int, int>, std::vector<int>> e2f;
    auto ekey = [](int a, int b) { return std::pair<int, int>{std::min(a, b), std::max(a, b)}; };
    for (int f = 0; f < nf; ++f)
      for (int i = 0; i < 3; ++i) e2f[ekey(F[f][i], F[f][(i + 1) % 3])].push_back(f);

    int boundary = 0;
    for (const auto& [k, fs] : e2f)
      if (fs.size() == 1) ++boundary;

    // Does face f traverse undirected edge (a,b) in the direction a->b?
    auto dirAB = [&](int f, int a, int b) {
      for (int i = 0; i < 3; ++i)
        if (F[f][i] == a && F[f][(i + 1) % 3] == b) return true;
      return false;
    };

    std::vector<int> comp(nf, -1);
    int ncomp = 0;
    for (int s = 0; s < nf; ++s) {
      if (comp[s] >= 0) continue;
      const int cid = ncomp++;
      std::vector<int> stack{s};
      comp[s] = cid;
      std::vector<int> members;
      while (!stack.empty()) {
        const int f = stack.back();
        stack.pop_back();
        members.push_back(f);
        for (int i = 0; i < 3; ++i) {
          const int a = F[f][i], b = F[f][(i + 1) % 3];
          for (const int g : e2f[ekey(a, b)]) {
            if (g == f) continue;
            // consistent iff g traverses the shared edge the opposite way
            const bool gForward = dirAB(g, a, b);
            if (comp[g] < 0) {
              if (gForward) std::swap(F[g][1], F[g][2]);  // flip to oppose f
              comp[g] = cid;
              stack.push_back(g);
            }
          }
        }
      }
      // outward orientation: signed volume of this component about the origin
      double vol = 0.0;
      for (const int f : members)
        vol += V[F[f][0]].dot(V[F[f][1]].cross(V[F[f][2]]));
      if (vol < 0)
        for (const int f : members) std::swap(F[f][1], F[f][2]);
    }
    return boundary;
  }

  // Edges shared by more than two triangles — a non-manifold "fin", which
  // orient()'s boundary count cannot see. Used to reject a blend that overlaps
  // itself (e.g. two fillets colliding along an exact tangency line).
  int nonManifoldEdges() const
  {
    std::map<std::pair<int, int>, int> use;
    auto ekey = [](int a, int b) { return std::pair<int, int>{std::min(a, b), std::max(a, b)}; };
    for (const auto& f : F)
      for (int i = 0; i < 3; ++i) ++use[ekey(f[i], f[(i + 1) % 3])];
    int n = 0;
    for (const auto& [k, c] : use)
      if (c > 2) ++n;
    return n;
  }

  // Face pairs the surface has laid back onto each other: an edge whose two faces
  // meet at a dihedral this close to 180 degrees is a zero-volume flap. It is the
  // one defect that leaves a solid closed, edge-manifold, of unchanged genus and
  // still wrong, so nothing else in this file can see it. Winding must already be
  // consistent (call after orient()), because the test is on the angle between
  // OUTWARD normals -- 180 only when the two point against each other.
  int foldPairs() const
  {
    std::map<std::pair<int, int>, std::vector<int>> use;
    std::vector<Vector3d> nrm(F.size());
    std::vector<double> area(F.size());
    for (size_t i = 0; i < F.size(); ++i) {
      const Vector3d n = (V[F[i][1]] - V[F[i][0]]).cross(V[F[i][2]] - V[F[i][0]]);
      area[i] = 0.5 * n.norm();
      nrm[i] = n.norm() > 1e-30 ? Vector3d(n.normalized()) : Vector3d::Zero();
      for (int k = 0; k < 3; ++k) {
        const int a = F[i][k], b = F[i][(k + 1) % 3];
        use[{std::min(a, b), std::max(a, b)}].push_back(static_cast<int>(i));
      }
    }
    const double cosFold = std::cos(kFoldDihedralDeg * M_PI / 180.0);
    int n = 0;
    for (const auto& [e, fs] : use) {
      if (fs.size() != 2) continue;
      // A triangle smaller than the weld quantisation has no reliable normal: its
      // direction is the rounding. Counting those reads folds on clean meshes.
      if (area[fs[0]] < kWeldTolMm * kWeldTolMm || area[fs[1]] < kWeldTolMm * kWeldTolMm) continue;
      if (nrm[fs[0]].dot(nrm[fs[1]]) < cosFold) ++n;
    }
    return n;
  }

  std::unique_ptr<PolySet> build() const
  {
    PolySetBuilder b(0, 0, 3, /*convex=*/false);
    for (const auto& f : F) {
      b.beginPolygon(3);
      b.addVertex(V[f[0]]);
      b.addVertex(V[f[1]]);
      b.addVertex(V[f[2]]);
      b.endPolygon();
    }
    return b.build();
  }
};

// Spherical linear interpolation between two unit vectors, t in [0,1]. Falls back
// to the linear blend when they are nearly parallel or nearly antiparallel (where
// the great-circle direction is ill-defined and the two are close enough that a
// straight blend is indistinguishable from the arc).
Vector3d slerpUnit(const Vector3d& a, const Vector3d& b, double t)
{
  const double d = std::clamp(a.dot(b), -1.0, 1.0);
  const double ang = std::acos(d);
  if (ang < 1e-6) return a;
  const double s = std::sin(ang);
  if (s < 1e-9) return ((1.0 - t) * a + t * b).normalized();
  return (std::sin((1.0 - t) * ang) / s) * a + (std::sin(t * ang) / s) * b;
}

// Intersection of two coplanar lines P1+λd1 and P2+λd2 (d1,d2 unit). Falls back
// to P1 when they are near-parallel (a straight boundary through the vertex).
Vector3d lineIntersect(const Vector3d& P1, const Vector3d& d1, const Vector3d& P2,
                       const Vector3d& d2)
{
  const Vector3d cx = d1.cross(d2);
  const double den = cx.squaredNorm();
  if (den < 1e-18) return P1;
  const Vector3d r = P2 - P1;
  const double lambda = r.cross(d2).dot(cx) / den;
  return P1 + lambda * d1;
}

// Where a vertex lands when the two feature edges bounding one of its face
// sectors are set back: the mitre of their offset lines, held to a mitre limit.
// U is the vertex; each line is given by its offset base, its unit direction
// along its edge, and the setback that offset it.
//
// The bare crossing is the answer for any honest corner, but it is conditioned
// only by the angle the two edges leave U at: it stands 1/sin(half-angle)
// setbacks out, so let them leave nearly the same way -- a tessellated
// near-tangent junction, where one crease family re-enters a vertex as a razor
// sector -- and the crossing races off down the boundary, tens of millimetres
// out for a one-millimetre fillet, taking the face's retreat and every strip
// foot with it. That point is not a seat: a ball of the blend's own radius
// seated at U touches the face only within the two setback bands, so nothing a
// couple of setbacks out is reachable from U at all, and past there the blend is
// the neighbouring edge's strip rather than this vertex's mitre.
//
// So the mitre is capped at kMitreLimit setbacks, by clamping the slide along
// the deeper offset line -- which keeps that line's retreat exactly and gives up
// only the slide the near-parallel crossing invented. The setback and the slide
// are the legs of a right angle on that line, so the clamp meets the raw mitre
// at the limit and the map stays continuous; and where the two setbacks are
// equal, either line clamps to the same point, so sections that welded on the
// raw mitre weld on the clamped one.
Vector3d mitre(const Vector3d& U, const Vector3d& b1, const Vector3d& d1, double s1,
               const Vector3d& b2, const Vector3d& d2, double s2)
{
  const Vector3d M = lineIntersect(b1, d1, b2, d2);
  const double limit = kMitreLimit * std::max(s1, s2);
  if ((M - U).norm() <= limit) return M;
  const bool first = s1 >= s2;
  const Vector3d& b = first ? b1 : b2;
  const Vector3d& d = first ? d1 : d2;
  const double s = first ? s1 : s2;
  const double slide = (M - b).dot(d);
  const double cap = std::sqrt(std::max(limit * limit - s * s, 0.0));
  return b + std::clamp(slide, -cap, cap) * d;
}

// Split the mesh edge (a,b) into `parts` equal collinear pieces, re-fanning each
// of its incident triangles from their apex. Geometry-exact — the new vertices
// lie on the segment and every sub-triangle is coplanar with the parent it came
// from, so the solid, its dihedrals and its surface grouping are unchanged — and
// manifold-preserving, because the two incident triangles are split in step with
// the edge (no T-junction) and share the same interior vertices. The parent's
// outward normal and source id ride onto every sub-triangle.
void splitEdge(MergedMesh& m, int a, int b, int parts)
{
  if (parts < 2) return;
  const Vector3d A = m.pos[a], B = m.pos[b];
  std::vector<int> interior;  // new vertex ids, ordered from a toward b
  interior.reserve(parts - 1);
  for (int j = 1; j < parts; ++j) {
    interior.push_back(static_cast<int>(m.pos.size()));
    m.pos.push_back(A + (B - A) * (static_cast<double>(j) / parts));
  }
  std::vector<Tri> next;
  next.reserve(m.tris.size() + 2 * parts);
  for (const Tri& T : m.tris) {
    int ia = -1, ib = -1;
    for (int i = 0; i < 3; ++i) {
      if (T.v[i] == a) ia = i;
      if (T.v[i] == b) ib = i;
    }
    if (ia < 0 || ib < 0) { next.push_back(T); continue; }
    const int w = T.v[3 - ia - ib];          // the third slot (0+1+2 = 3)
    const bool aToB = (ia + 1) % 3 == ib;    // does this triangle wind a -> b ?
    // The chain of stations across the split edge, in the triangle's own winding.
    std::vector<int> chain;
    chain.reserve(parts + 1);
    chain.push_back(aToB ? a : b);
    if (aToB)
      for (int k = 0; k < static_cast<int>(interior.size()); ++k) chain.push_back(interior[k]);
    else
      for (int k = static_cast<int>(interior.size()) - 1; k >= 0; --k) chain.push_back(interior[k]);
    chain.push_back(aToB ? b : a);
    // Fan from w, preserving the (edge-direction, w) orientation and normal/id.
    for (int k = 0; k + 1 < static_cast<int>(chain.size()); ++k)
      next.push_back(Tri{{chain[k], chain[k + 1], w}, T.normal, T.originalID});
  }
  m.tris = std::move(next);
}

// The crease edges of a mesh, and which of them the blend will act on. Shared by
// the along-sweep station floor (which splits the long ones) and the main build
// (which seats a bead on them), so both agree on exactly which edges are blended.
//
// `crease` is every edge that clears the surface threshold — a surface boundary,
// carrying its sign and dihedral. `eligible` is the subset to blend: start from
// every edge that ALSO clears the feature threshold, then follow the crease into
// the neighbour that turns least (within kCreaseFollowMaxTurnDeg) and does not run
// straight through the shared vertex, so a shallow tangent stretch continuing a
// genuine feature — a tee's intersection loop, its tangent sides included — is
// taken in full, while a tessellation seam or a gentle fold that never reaches the
// feature threshold is left sharp. This is the one place the two thresholds meet:
// the feature threshold decides eligibility, the surface threshold bounds the
// crease. Tool-sign and brush filters are the caller's.
struct CreaseSelection
{
  std::map<EdgeKey, EdgeClass> crease;
  std::set<EdgeKey> eligible;
};

CreaseSelection selectCreaseEdges(const MergedMesh& m,
                                  const std::map<EdgeKey, std::vector<int>>& adj,
                                  double thresholdDeg, double surfaceThresholdDeg)
{
  CreaseSelection sel;
  std::map<int, std::vector<int>> vinc;  // vertex -> crease-edge neighbour vertices
  for (const auto& [key, ts] : adj) {
    if (ts.size() != 2) continue;
    const EdgeClass ec = classifyEdge(m, key, m.tris[ts[0]], m.tris[ts[1]]);
    if (!isFeatureAngle(ec.dihedralDeg, surfaceThresholdDeg)) continue;
    sel.crease[key] = ec;
    vinc[key.first].push_back(key.second);
    vinc[key.second].push_back(key.first);
  }
  const double cosTurn = std::cos(kCreaseFollowMaxTurnDeg * M_PI / 180.0);
  // ...but a tessellation seam that grazes the crease at a shallow angle passes
  // the turn test. It is told apart by passing STRAIGHT THROUGH the vertex: it has
  // another edge here nearly opposite to it, so it is the middle of a line rather
  // than a crease turning. (A real crease turns at such a vertex, so its
  // continuation has no opposite partner other than the edge we arrived on.) This
  // is what stops the follow from running down a cylinder's vertical seam where
  // the intersection curve dives steeply past it at a tangent junction.
  const double cosStraight = std::cos(kCreaseStraightDeg * M_PI / 180.0);
  auto straightThrough = [&](int u, int v, int w) {
    const Vector3d dw = (m.pos[w] - m.pos[v]).normalized();
    // A collinear partner at v (other than the edge we arrived on) means the
    // candidate is the middle of a straight line through v — a seam, not a turn.
    for (const int z : vinc[v]) {
      if (z == u || z == w) continue;
      if ((m.pos[z] - m.pos[v]).normalized().dot(dw) <= -cosStraight) return true;
    }
    // A seam radiating from the junction has no partner at its start vertex, but
    // it runs straight on past its far end w — where a collinear continuation
    // (other than back to v) marks it a seam too. A seam is only ever picked up
    // by TURNING onto it, though, so this end of the test is off when the
    // candidate carries on straight from the edge we arrived on: an intersection
    // curve running out to a tangency straightens as it goes, and every one of
    // its own continuations is collinear like a seam's.
    const Vector3d din = (m.pos[v] - m.pos[u]).normalized();
    if (din.dot(dw) >= cosStraight) return false;
    for (const int z : vinc[w]) {
      if (z == v) continue;
      if ((m.pos[z] - m.pos[w]).normalized().dot(dw) >= cosStraight) return true;
    }
    return false;
  };
  std::vector<std::pair<int, int>> walk;  // directed: arrived at .second via .first
  for (const auto& [key, ec] : sel.crease)
    if (isFeatureAngle(ec.dihedralDeg, thresholdDeg) && sel.eligible.insert(key).second) {
      walk.push_back({key.first, key.second});
      walk.push_back({key.second, key.first});
    }
  while (!walk.empty()) {
    const auto [u, v] = walk.back();
    walk.pop_back();
    const Vector3d din = (m.pos[v] - m.pos[u]).normalized();
    for (const int w : vinc[v]) {
      if (w == u) continue;
      const EdgeKey nk{std::min(v, w), std::max(v, w)};
      if (sel.eligible.count(nk)) continue;
      if (din.dot((m.pos[w] - m.pos[v]).normalized()) < cosTurn) continue;
      if (straightThrough(u, v, w)) continue;
      sel.eligible.insert(nk);
      walk.push_back({v, w});
    }
  }
  return sel;
}

// The second density floor (`along-sweep-stations.md`): before the blend runs,
// give every long selected crease enough stations that a straight fillet holds a
// constant cross-section instead of tapering between its two corner-distorted
// ends. Split each edge in `edges` longer than `cap` into ceil(len/cap) equal
// pieces. `edges` is the eligible set (selectCreaseEdges), tool-sign filtered by
// the caller — the exact edges the build will blend, so a shallow tangent stretch
// carried into the selection by crease-following is densified too, not only the
// edges that independently clear the feature threshold. Splitting only ever
// shortens; it never turns a seam into a feature, so one pass is complete.
void subdivideLongCreaseEdges(MergedMesh& m, const std::set<EdgeKey>& edges, double cap,
                              std::map<int, EdgeKey>* stationOf = nullptr)
{
  if (!(cap > 0) || edges.empty()) return;
  struct Split { int a, b, parts; };
  std::vector<Split> todo;
  for (const EdgeKey& key : edges) {
    const double len = (m.pos[key.second] - m.pos[key.first]).norm();
    if (len <= cap) continue;
    int parts = static_cast<int>(std::ceil(len / cap));
    parts = std::min(parts, kMaxEdgeSplitParts);  // backstop against a pathological count
    todo.push_back({key.first, key.second, parts});
  }
  // Vertex ids are only ever appended, so the endpoints collected above stay
  // valid; splitEdge rescans the current triangle list, so shared triangles
  // split by an earlier edge are handled correctly.
  for (const Split& s : todo) {
    const int first = static_cast<int>(m.pos.size());
    splitEdge(m, s.a, s.b, s.parts);
    // Which crease each new station came off. The brush is a statement about the
    // model's own edges, and the stations are this pass's private business, so the
    // selection has to be able to ask the question of the edge rather than of the
    // piece it was cut into.
    if (stationOf)
      for (int v = first; v < static_cast<int>(m.pos.size()); ++v)
        (*stationOf)[v] = EdgeKey{std::min(s.a, s.b), std::max(s.a, s.b)};
  }
}

// Ear-clip a planar polygon given by position, in the plane of `nrm`, into index
// triples wound to agree with `nrm`. False if the polygon is degenerate or leaves
// an ear the clip cannot find.
bool earClipPlanar(const std::vector<Vector3d>& p, const Vector3d& nrm,
                   std::vector<std::array<int, 3>>& tris)
{
  const int n = static_cast<int>(p.size());
  if (n < 3) return false;
  Vector3d ex = (std::abs(nrm.x()) < 0.9 ? Vector3d::UnitX() : Vector3d::UnitY());
  ex = (ex - ex.dot(nrm) * nrm).normalized();
  const Vector3d ey = nrm.cross(ex);
  std::vector<Vector2d> q(n);
  for (int i = 0; i < n; ++i) q[i] = {p[i].dot(ex), p[i].dot(ey)};
  double area = 0;  // signed area to fix winding
  for (int i = 0; i < n; ++i)
    area += q[i].x() * q[(i + 1) % n].y() - q[(i + 1) % n].x() * q[i].y();
  if (std::abs(area) < 1e-15) return false;
  std::vector<int> ring(n);
  for (int i = 0; i < n; ++i) ring[i] = (area < 0) ? (n - 1 - i) : i;
  auto cross2 = [](const Vector2d& a, const Vector2d& b, const Vector2d& c) {
    return (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
  };
  // Clip the roundest ear each round, not the first one round the ring: these
  // triangles are about to have their corners set back to their mitres, and a
  // sliver spanning a reflex corner turns inside out when they move.
  auto roundness = [&](const Vector2d& a, const Vector2d& b, const Vector2d& c) {
    const double e0 = (b - a).squaredNorm(), e1 = (c - b).squaredNorm(),
                 e2 = (a - c).squaredNorm();
    const double longest = std::max({e0, e1, e2});
    return longest > 0 ? std::abs(cross2(a, b, c)) / longest : 0.0;
  };
  tris.clear();
  int guard = 0;
  while (ring.size() > 3 && guard++ < 4 * n) {
    const int m2 = static_cast<int>(ring.size());
    int bestI = -1;
    double bestR = 0;
    for (int i = 0; i < m2; ++i) {
      const int ia = ring[(i + m2 - 1) % m2], ib = ring[i], ic = ring[(i + 1) % m2];
      if (cross2(q[ia], q[ib], q[ic]) <= 1e-12) continue;  // reflex or collinear
      bool ear = true;
      for (int j = 0; j < m2 && ear; ++j) {
        const int k = ring[j];
        if (k == ia || k == ib || k == ic) continue;
        ear = !(cross2(q[ia], q[ib], q[k]) >= 0 && cross2(q[ib], q[ic], q[k]) >= 0 &&
                cross2(q[ic], q[ia], q[k]) >= 0);
      }
      if (!ear) continue;
      const double r = roundness(q[ia], q[ib], q[ic]);
      if (bestI < 0 || r > bestR) {
        bestI = i;
        bestR = r;
      }
    }
    if (bestI < 0) return false;
    tris.push_back({ring[(bestI + m2 - 1) % m2], ring[bestI], ring[(bestI + 1) % m2]});
    ring.erase(ring.begin() + bestI);
  }
  if (ring.size() == 3) tris.push_back({ring[0], ring[1], ring[2]});
  // The clip ran on the winding-corrected copy; wind every triangle with nrm.
  for (auto& t : tris)
    if ((p[t[1]] - p[t[0]]).cross(p[t[2]] - p[t[0]]).dot(nrm) < 0) std::swap(t[1], t[2]);
  return true;
}

// The ring of vertices around u, in the fan's own winding: each incident triangle
// contributes the directed edge opposite u, and those edges chain into one cycle.
// Empty if they do not (u is not a clean manifold fan).
std::vector<int> ringAround(const MergedMesh& m, const std::vector<int>& tris, int u)
{
  std::map<int, int> nextOf;
  for (const int t : tris) {
    const auto& v = m.tris[t].v;
    for (int i = 0; i < 3; ++i)
      if (v[i] == u && !nextOf.emplace(v[(i + 1) % 3], v[(i + 2) % 3]).second) return {};
  }
  if (nextOf.size() != tris.size()) return {};
  std::vector<int> ring{nextOf.begin()->first};
  for (size_t k = 0; k < nextOf.size(); ++k) {
    const auto it = nextOf.find(ring.back());
    if (it == nextOf.end()) return {};
    if (it->second == ring.front()) break;
    ring.push_back(it->second);
  }
  return ring.size() == nextOf.size() ? ring : std::vector<int>{};
}

// Dissolve the vertices the blend swallows.
//
// A face triangulation may carry a vertex no crease touches — a union leaves one
// wherever it merges two coplanar faces into one. Such a vertex has no boundary to
// mitre against, so the inset leaves it exactly where it is; where it happens to lie
// within a blended edge's setback that is inside the region the strip (or, at a
// corner, the corner patch) now covers, and the surface triangles around it lap over
// that region and fold back on themselves — a zero-volume flap lying in the face,
// edge-manifold and closed, so nothing downstream of the blend refuses it.
//
// Dropping the vertex first costs nothing: its whole fan is one flat piece of face,
// so re-triangulating the ring it leaves behind reproduces the same face, with the
// same boundary, out of vertices the inset can all seat. Geometry-exact, like
// splitEdge — the solid, its dihedrals and its surface grouping are unchanged.
//
// `curvedFans` extends the same surgery to a swallowed vertex whose fan is one
// smooth wall rather than one flat face. There the cut is an approximation, not an
// identity, so it is offered rather than taken: the caller builds with it and
// without it and keeps whichever folds less. Returns whether any such fan was cut,
// so a model where the offer is empty is not built twice for nothing.
bool dissolveSwallowedVertices(MergedMesh& m, const std::map<EdgeKey, EdgeClass>& crease,
                               const std::set<EdgeKey>& blended, double size, bool isChamfer,
                               bool curvedFans)
{
  bool cutCurved = false;
  if (blended.empty()) return cutCurved;
  std::set<int> onCrease;
  for (const auto& [key, ec] : crease) {
    onCrease.insert(key.first);
    onCrease.insert(key.second);
  }
  // How far each blended edge sets its faces back — the band it takes over.
  std::vector<std::pair<EdgeKey, double>> band;
  for (const EdgeKey& key : blended) {
    const auto it = crease.find(key);
    if (it == crease.end()) continue;
    band.emplace_back(key, isChamfer ? size
                                     : size * std::tan(0.5 * it->second.dihedralDeg * M_PI / 180.0));
  }

  // A fan is one smooth wall when every triangle in it is within this of the fan's
  // own averaged normal — the same near-tangency that groups facets into one
  // surface, asked of a fan instead of an edge.
  const double cosGroup = std::cos(kDefaultSurfaceThresholdDeg * M_PI / 180.0);

  for (int u = 0; u < static_cast<int>(m.pos.size()); ++u) {
    if (onCrease.count(u)) continue;  // it has a boundary of its own to mitre against
    const Vector3d p = m.pos[u];
    bool swallowed = false;
    for (const auto& [key, sb] : band) {
      const Vector3d a = m.pos[key.first], ab = m.pos[key.second] - a;
      const double l2 = ab.squaredNorm();
      const double s = l2 > 1e-18 ? std::clamp((p - a).dot(ab) / l2, 0.0, 1.0) : 0.0;
      // The band is closed at sb: a vertex sitting exactly on the setback distance
      // lands exactly on the inset line the strip's foot runs along, so the surface
      // triangles through it come out collinear and can carry neither their own area
      // nor the foot. That is the same swallowing as one strictly inside, and it is
      // what a stray vertex a whole radius from a corner hits on an exact size.
      if ((p - Vector3d(a + s * ab)).norm() < sb + 1e-9) { swallowed = true; break; }
    }
    if (!swallowed) continue;

    std::vector<int> tris;
    for (size_t t = 0; t < m.tris.size(); ++t)
      for (const int w : m.tris[t].v)
        if (w == u) { tris.push_back(static_cast<int>(t)); break; }
    if (tris.size() < 3) continue;
    Vector3d nrm = m.tris[tris.front()].normal;
    bool flat = true;
    for (const int t : tris) flat = flat && m.tris[t].normal.dot(nrm) > 1 - 1e-12;
    if (!flat) {
      // Not one flat fan. Dropping it moves the surface, so it is only offered
      // where the fan is one smooth wall — every triangle within the grouping
      // threshold of the fan's own averaged normal, which is the same test that
      // decides these facets are approximating one wall in the first place. The
      // re-cut then replaces a facet corner with the chord across it, a change of
      // the tessellation's own order and made entirely inside the band the blend
      // is about to cover.
      //
      // WITHOUT THIS a curved wall keeps every vertex the retreat swallows. The
      // wall's face triangles then reach back from the retreated seat over the
      // ring below it, come out turned over, and lie on the wall as a fold — one
      // per crease vertex, so the count scales with $fn. It is the flat fan's
      // fault exactly, on a wall too finely faceted for the fan to be flat.
      if (!curvedFans) continue;
      Vector3d nsum = Vector3d::Zero();
      for (const int t : tris) nsum += m.tris[t].normal;
      if (nsum.norm() < 1e-12) continue;
      nrm = nsum.normalized();
      bool smooth = true;
      for (const int t : tris) smooth = smooth && m.tris[t].normal.dot(nrm) > cosGroup;
      if (!smooth) continue;
    }

    const std::vector<int> ring = ringAround(m, tris, u);
    if (ring.size() < 3) continue;
    std::vector<Vector3d> poly;
    poly.reserve(ring.size());
    for (const int w : ring) poly.push_back(m.pos[w]);
    std::vector<std::array<int, 3>> flatTris;
    if (!earClipPlanar(poly, nrm, flatTris)) continue;

    // On a curved fan the re-cut is an approximation, and one ear can be a long
    // chord across the whole ring — a triangle that is nowhere near the wall it
    // replaced. Read the cut back before taking it: every new triangle has to sit
    // within the same grouping threshold of the fan's normal that let the fan be
    // dissolved at all. Without the read-back a fine tessellation dissolves ring
    // after ring, each cut widening the next one's ring, and the wall ends up
    // coarser than the blend that covers it — measurably more folded, not fewer.
    std::vector<Vector3d> newNrm;
    if (!flat) {
      newNrm.reserve(flatTris.size());
      bool faithful = true;
      for (const auto& t : flatTris) {
        const Vector3d a = m.pos[ring[t[0]]], b = m.pos[ring[t[1]]], c = m.pos[ring[t[2]]];
        const Vector3d n = (b - a).cross(c - a);
        if (n.norm() < 1e-18) { faithful = false; break; }
        newNrm.push_back(n.normalized());
        if (newNrm.back().dot(nrm) <= cosGroup) { faithful = false; break; }
      }
      if (!faithful) continue;
      cutCurved = true;
    }

    const uint32_t id = m.tris[tris.front()].originalID;
    const std::set<int> drop(tris.begin(), tris.end());
    std::vector<Tri> next;
    next.reserve(m.tris.size() - tris.size() + flatTris.size());
    for (size_t t = 0; t < m.tris.size(); ++t)
      if (!drop.count(static_cast<int>(t))) next.push_back(m.tris[t]);
    // A flat fan keeps the plane it always had; a curved one carries each new
    // triangle's own normal, because the classification downstream reads this
    // field and a fan-wide average would tell it the cut is flatter than it is.
    for (size_t k = 0; k < flatTris.size(); ++k) {
      const auto& t = flatTris[k];
      next.push_back(Tri{{ring[t[0]], ring[t[1]], ring[t[2]]}, flat ? nrm : newNrm[k], id});
    }
    m.tris = std::move(next);
  }
  return cutCurved;
}

// --- the face footprint, in the face's own plane ---------------------------
//
// A planar face's footprint is what is left of it once every round-over along its
// boundary has retreated: one outer loop, plus one inner loop per hole the face
// carries. It is a 2-D object and the faults it has are 2-D faults — a boundary
// that crosses itself, a triangulation whose diagonals no longer fit the region
// their vertices moved to — so it is worked in the face plane rather than read off
// the moved triangles one at a time.

struct Basis2
{
  Vector3d o, ex, ey;
};

Basis2 planeBasis(const Vector3d& o, const Vector3d& nrm)
{
  Vector3d ex = (std::abs(nrm.x()) < 0.9 ? Vector3d::UnitX() : Vector3d::UnitY());
  ex = (ex - ex.dot(nrm) * nrm).normalized();
  return {o, ex, nrm.cross(ex)};
}

Vector2d project2(const Basis2& b, const Vector3d& p)
{
  const Vector3d d = p - b.o;
  return {d.dot(b.ex), d.dot(b.ey)};
}

Vector3d unproject2(const Basis2& b, const Vector2d& p)
{
  return b.o + p.x() * b.ex + p.y() * b.ey;
}

double cross2(const Vector2d& a, const Vector2d& b, const Vector2d& c)
{
  return (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
}

// Twice the signed area of a closed 2-D loop; positive is counter-clockwise, which
// is the winding a face's outer loop carries about its own normal.
double loopArea2(const std::vector<Vector2d>& p)
{
  double a = 0;
  for (size_t i = 0; i < p.size(); ++i) {
    const Vector2d& q = p[(i + 1) % p.size()];
    a += p[i].x() * q.y() - q.x() * p[i].y();
  }
  return a;
}

// How far a point stands outside a closed 2-D loop: zero when it is inside or on
// the loop, otherwise its distance to the nearest link. Inside is by crossing
// number; the distance is what tells a seat that has raced off the face from one
// that has merely spilled a whisker onto the next facet of a tessellated wall,
// which is the same surface in every sense but the surface id.
double loopEscape(const std::vector<Vector2d>& p, const Vector2d& q)
{
  const size_t n = p.size();
  bool in = false;
  double dmin = 1e30;
  for (size_t i = 0; i < n; ++i) {
    const Vector2d& a = p[i];
    const Vector2d& b = p[(i + 1) % n];
    const Vector2d ab = b - a;
    const double l2 = ab.squaredNorm();
    const double t = l2 > 0 ? std::clamp((q - a).dot(ab) / l2, 0.0, 1.0) : 0.0;
    dmin = std::min(dmin, (q - (a + t * ab)).norm());
    if ((a.y() > q.y()) != (b.y() > q.y())) {
      const double x = a.x() + (q.y() - a.y()) / (b.y() - a.y()) * ab.x();
      if (q.x() < x) in = !in;
    }
  }
  return in ? 0.0 : dmin;
}

// Where two segments cross each other properly — strictly inside both, so segments
// that merely share an endpoint (every adjacent pair of links does) are not
// crossings. eps is a length, in the units the points are in.
bool segCross2(const Vector2d& a, const Vector2d& b, const Vector2d& c, const Vector2d& d,
               double eps, Vector2d& x)
{
  const Vector2d r = b - a, s = d - c;
  const double den = r.x() * s.y() - r.y() * s.x();
  const double rl = r.norm(), sl = s.norm();
  if (rl < eps || sl < eps) return false;
  if (std::abs(den) < eps * eps * 1e-6) return false;  // parallel, or as good as
  const Vector2d ac = c - a;
  const double t = (ac.x() * s.y() - ac.y() * s.x()) / den;
  const double u = (ac.x() * r.y() - ac.y() * r.x()) / den;
  const double te = eps / rl, ue = eps / sl;
  if (t <= te || t >= 1 - te || u <= ue || u >= 1 - ue) return false;
  x = a + t * r;
  return true;
}

// Ear-clip a 2-D ring given as indices into p, counter-clockwise. Unlike
// earClipPlanar this one is fed rings that deliberately visit one position twice —
// the two ends of a hole bridge — so a point coincident with one of the ear's own
// corners is not treated as blocking it. Returns false if no ear is found, which
// is the caller's signal that the ring is not simple and the patch must be refused.
bool earClip2(const std::vector<Vector2d>& p, std::vector<int> ring,
              std::vector<std::array<int, 3>>& tris, double eps)
{
  const int n = static_cast<int>(ring.size());
  if (n < 3) return false;
  tris.clear();
  int guard = 0;
  while (ring.size() > 3 && guard++ < 4 * n) {
    const int k = static_cast<int>(ring.size());
    bool clipped = false;
    for (int i = 0; i < k; ++i) {
      const int ia = ring[(i + k - 1) % k], ib = ring[i], ic = ring[(i + 1) % k];
      const Vector2d &A = p[ia], &B = p[ib], &C = p[ic];
      if (cross2(A, B, C) <= eps * eps) continue;  // reflex, or too thin to be an ear
      bool ear = true;
      for (int j = 0; j < k && ear; ++j) {
        const int q = ring[j];
        if (q == ia || q == ib || q == ic) continue;
        const Vector2d& P = p[q];
        if ((P - A).norm() < eps || (P - B).norm() < eps || (P - C).norm() < eps) continue;
        if (cross2(A, B, P) >= 0 && cross2(B, C, P) >= 0 && cross2(C, A, P) >= 0) ear = false;
      }
      if (!ear) continue;
      tris.push_back({ia, ib, ic});
      ring.erase(ring.begin() + i);
      clipped = true;
      break;
    }
    if (!clipped) return false;
  }
  if (ring.size() == 3) tris.push_back({ring[0], ring[1], ring[2]});
  return true;
}

// Splice a hole loop into the outer ring along a bridge — the classical way to hand
// a polygon with holes to an ear clipper. The bridge is the shortest ring/hole
// vertex pair whose segment crosses no edge of either loop; a heuristic, which is
// why the caller checks the finished triangulation's area against the polygon's own
// rather than trusting it.
bool bridgeHole(std::vector<int>& ring, const std::vector<int>& hole,
                const std::vector<Vector2d>& p, double eps)
{
  auto blocked = [&](const Vector2d& a, const Vector2d& b, const std::vector<int>& loop) {
    Vector2d x;
    for (size_t i = 0; i < loop.size(); ++i)
      if (segCross2(a, b, p[loop[i]], p[loop[(i + 1) % loop.size()]], eps, x)) return true;
    return false;
  };
  int bi = -1, bj = -1;
  double best = std::numeric_limits<double>::max();
  for (size_t i = 0; i < ring.size(); ++i)
    for (size_t j = 0; j < hole.size(); ++j) {
      const double d = (p[ring[i]] - p[hole[j]]).squaredNorm();
      if (d >= best) continue;
      if (blocked(p[ring[i]], p[hole[j]], ring) || blocked(p[ring[i]], p[hole[j]], hole)) continue;
      best = d;
      bi = static_cast<int>(i);
      bj = static_cast<int>(j);
    }
  if (bi < 0) return false;
  std::vector<int> next(ring.begin(), ring.begin() + bi + 1);
  for (size_t k = 0; k < hole.size(); ++k) next.push_back(hole[(bj + k) % hole.size()]);
  next.push_back(hole[bj]);
  next.insert(next.end(), ring.begin() + bi, ring.end());
  ring = std::move(next);
  return true;
}

// Weld the crease vertices the blend cannot tell apart.
//
// A blended edge carries a strip, and the strip is spanned between one
// cross-section at each end. Where a boolean leaves a crease edge orders of
// magnitude shorter than the blend — two cylinders merging leave a sliver a few
// microns long at the waist where their walls graze, and the crease chain steps
// through it — that strip is a whole fillet section's worth of surface hung on an
// edge it cannot resolve. Its two sections sit at essentially the same place and
// point in different directions, so the strip is a blade, and it sweeps through the
// strips on either side of it: they lap, and the lap is a fold.
//
// The crease is one crease and the turn is one turn; the sliver is the
// tessellation's, not the model's. Welding its two ends puts the turn at a single
// vertex, where the corner patch is built to take it. The cost is bounded by the
// threshold: nothing moves further than a fraction of the blend size, well under
// the resolution of the surface being built.
//
// A collapse is refused unless the link condition holds — a and b share exactly the
// two vertices opposite the edge — because collapsing across any other shared
// neighbour folds the surrounding fan onto itself and makes a non-manifold mesh out
// of a manifold one.
void weldSliverCreaseEdges(MergedMesh& m, const std::set<EdgeKey>& blended, double limit)
{
  if (blended.empty() || !(limit > 0)) return;
  std::vector<int> remap(m.pos.size());
  for (size_t i = 0; i < remap.size(); ++i) remap[i] = static_cast<int>(i);
  std::function<int(int)> find = [&](int a) {
    while (remap[a] != a) a = remap[a] = remap[remap[a]];
    return a;
  };

  bool any = false;
  for (const EdgeKey& key : blended) {
    const int a = find(key.first), b = find(key.second);
    if (a == b) continue;
    if ((m.pos[a] - m.pos[b]).norm() >= limit) continue;

    std::map<int, std::vector<int>> ring;  // neighbour -> the triangles carrying it
    std::set<int> na, nb, both;
    for (const Tri& T : m.tris) {
      int has = 0;
      for (const int v : T.v) {
        if (find(v) == a) has |= 1;
        if (find(v) == b) has |= 2;
      }
      if (!has) continue;
      for (const int v : T.v) {
        const int w = find(v);
        if (w == a || w == b) continue;
        (has & 1 ? na : nb).insert(w);
        if (has == 3) both.insert(w);
      }
    }
    std::set<int> shared;
    for (const int w : na)
      if (nb.count(w)) shared.insert(w);
    if (shared != both || shared.size() != 2) continue;  // link condition

    remap[b] = a;
    any = true;
  }
  if (!any) return;

  std::vector<Tri> next;
  next.reserve(m.tris.size());
  for (const Tri& T : m.tris) {
    const std::array<int, 3> v{find(T.v[0]), find(T.v[1]), find(T.v[2])};
    if (v[0] == v[1] || v[1] == v[2] || v[0] == v[2]) continue;  // collapsed away
    next.push_back(Tri{v, T.normal, T.originalID});
  }
  m.tris = std::move(next);
}

// The dominant blend construction. Fields captured once so the per-edge and
// per-vertex helpers read one context.
struct Blender
{
  const MergedMesh& m;
  const std::map<EdgeKey, std::vector<int>>& adj;
  const std::vector<int>& surfaceOf;  // one surface id per triangle
  double size;
  bool isChamfer;
  double thresholdDeg;
  const CurveDiscretizer& disc;
  int arcSegs = 4;  // arc segments per fillet cross-section, uniform across the
                    // whole blend so adjacent cross-sections always have equal
                    // point counts and every strip closes (set in buildBlend)

  std::set<EdgeKey> selected;                 // the edges this call acts on
  std::set<EdgeKey> feature;                  // all crease edges (selected or not)
  std::map<EdgeKey, bool> concaveOf;          // sign per selected edge
  OutMesh out;

  // Shared per-vertex cross-section, populated by prepareShared() for the
  // pass-through vertices of a smooth crease (see there). Where two consecutive
  // strips continue one crease across an interior tessellation seam, both must
  // seat on ONE inset point and ONE seat normal per side, or their tangent
  // points miss the 1e-6 weld and leave a facet dent. These override maps carry
  // that single answer, keyed by (vertex u, incident triangle t): every facet on
  // one side of the crease at u maps to the same value, so the surface pass, the
  // two strips, the corner skip and the kept-seam ribbon all agree. Junction and
  // corner vertices are deliberately left out (empty here), so their emitCorner
  // ring keeps the surface-bounded seating that closes it.
  std::map<std::pair<int, int>, Vector3d> insetOverride;
  std::map<std::pair<int, int>, Vector3d> normalOverride;

  // Inset points that a fold repair has collapsed onto a neighbour's, keyed the
  // same way. Populated by repairInsetFolds(); empty wherever the offset is
  // well-behaved, which is most of the bench. Read first by insetForTri so every
  // consumer — surface pass, strips, seams, corner patches — moves together.
  std::map<std::pair<int, int>, Vector3d> insetCollapse;

  // Face triangles whose emitted corners are not their own vertices' insets,
  // because flipInvertedInsets has re-cut the diagonal they share with a
  // neighbour. Keyed by source triangle; the three points are already inset.
  std::map<int, std::array<Vector3d, 3>> surfaceOverride;

  // Vertices where selected edges of BOTH signs meet — the mixed corners whose
  // strips otherwise blade. Populated by computeMixedVerts() before any emit.
  // Everything keyed off this set (the pulled-in cross-section, the surface
  // re-triangulation, the ring's arc endpoints) is inert at every other vertex,
  // so single-sign caps and pass-through welds are untouched.
  std::set<int> mixedVerts;

  // Same-sign vertices seated at the same pulled-in station, populated beside
  // mixedVerts when turnPull is on. Everything that asks "is this vertex seated at a
  // station" asks pulled() and sees both; only the exact-torus reseat in
  // computeCornerStations stays on mixedVerts, the corner it reseats being a mixed
  // one by definition.
  std::set<int> turnVerts;

  // Whether same-sign turning corners take the pull-in too. Off by default: it is
  // the right seat where two strips lap on the inside of a turn and the wrong one
  // where they were welding happily, so the driver turns it on only for a mesh that
  // came out folded and keeps the result only if fewer folds came with it.
  bool turnPull = false;

  bool pulled(int u) const { return mixedVerts.count(u) || turnVerts.count(u); }

  // The size gate's verdict, filled by run() when gateSize is on: the selected
  // creases whose faces the setbacks have consumed (overRoundEdges). Non-empty means
  // nothing was emitted — the caller drops these from the selection, leaving them
  // sharp for the kept-seam ribbon, and builds again.
  bool gateSize = true;
  std::set<EdgeKey> overRound;

  // Whether to seat mixed corners with the pulled-in cross-section (the blade fix)
  // or the baseline mitre. The driver runs the pulled build first and, only if it
  // leaves the mesh open, re-runs with this false — so an oblique or crowded corner
  // the pull-in cannot weld falls back to exactly the baseline blend, never worse.
  bool pullIn = true;

  // How far along the edge to seat a mixed corner's pulled-in station, as a
  // fraction of the deepest face mitre (see kPullStationFrac). 1.0 is the
  // full-mitre seat (the conservative fallback tier); the driver runs the tighter
  // default first so the round-overs flow into the corner.
  double stationFrac = kPullStationFrac;

  // Convex edges reseated to their full mitre, keyed (u, x). At a corner where the
  // two round-overs and the crease close over the vertex as one exact piece of
  // surface (emitCornerTube) that piece begins exactly at the full mitre, so the
  // strips have to end there for their sections to be its first and last
  // cross-sections. Populated by computeCornerStations(), empty everywhere else.
  std::map<std::pair<int, int>, double> stationOverride;

  // One tally per terminal path of emitCorner; every control-flow exit there
  // increments exactly one, so the fields sum to the number of corner vertices.
  struct CornerCounts
  {
    int tube = 0, capTri = 0, cap = 0, coons = 0, saddle = 0, flat = 0, fan = 0, weld = 0, none = 0;
    int total() const { return tube + capTri + cap + coons + saddle + flat + fan + weld + none; }
  };
  CornerCounts cornerCounts;

  void computeMixedVerts()
  {
    if (!pullIn) return;  // baseline seating: no vertex is treated as a pull-in corner
    std::map<int, std::pair<bool, bool>> sign;  // vertex -> (anyConcave, anyConvex)
    for (const auto& [e, concave] : concaveOf) {
      auto& sa = sign[e.first];
      auto& sb = sign[e.second];
      (concave ? sa.first : sa.second) = true;
      (concave ? sb.first : sb.second) = true;
    }
    // A vertex where a feature edge is kept sharp (refused by the size gate, the
    // sign filter or a brush) is left to the baseline seating: the pull-in coordinates
    // the selected strips against each other, but a kept edge is sewn by the flat
    // kept-seam ribbon, which seats on the un-pulled mitre — pulling the neighbouring
    // strips off it would open the seam. Those corners keep the blade; the clean
    // fully-selected corners (a box step, an L reflex, a rib end) get the fix.
    std::set<int> partial;
    for (const auto& e : feature)
      if (!selected.count(e)) { partial.insert(e.first); partial.insert(e.second); }
    for (const auto& [v, s] : sign)
      if (s.first && s.second && !partial.count(v)) mixedVerts.insert(v);
    if (!turnPull) return;

    // The blade is not exclusive to a mixed corner.
    //
    // A cross-section is spanned between its two feet, and each foot sits at its own
    // face's mitre -- its own distance along the edge. Where the two faces mitre to
    // very different depths the section is not a slice across the edge but a blade
    // twisted along it, and two blades meeting at a turning vertex sweep through
    // each other: the strips lap on the inside of the turn and come back as a fold.
    // Mixed signs make that happen dramatically, but they are not what causes it -- a
    // crease running off a plate and up a boss turns the same way with one sign
    // throughout. The same seat answers it: both feet at one station, which
    // un-twists the blade and leaves the wedge to the corner patch already built
    // there.
    //
    // It is not free, which is why it is a tier and not the default: a corner whose
    // strips were not lapping is pulled back for nothing and the bead necks.
    for (int u = 0; u < static_cast<int>(m.pos.size()); ++u) {
      if (mixedVerts.count(u) || partial.count(u)) continue;
      std::vector<int> via;
      for (const auto& [key, ts] : adj)
        if ((key.first == u || key.second == u) && selected.count(key))
          via.push_back(key.first == u ? key.second : key.first);
      if (via.size() < 2) continue;
      // Two sections that already weld are one crease passing through, and moving
      // one side of a weld the other side does not see would open it.
      if (via.size() == 2 &&
          sectionsWeld(edgeCrossSection(u, via[0]), edgeCrossSection(u, via[1])))
        continue;
      turnVerts.insert(u);
    }
  }

  bool isSelected(int a, int b) const
  {
    return selected.count({std::min(a, b), std::max(a, b)}) > 0;
  }

  // The setback along each face for one selected edge: r*tan(dihedral/2) for a
  // fillet arc, the setback t itself for a flat chamfer.
  double setback(const EdgeKey& e) const
  {
    const auto& ts = adj.at(e);
    const double dihedralDeg =
      std::acos(std::clamp(m.tris[ts[0]].normal.dot(m.tris[ts[1]].normal), -1.0, 1.0)) * 180.0 /
      M_PI;
    if (isChamfer) return size;
    return size * std::tan(0.5 * dihedralDeg * M_PI / 180.0);
  }

  // The triangle incident to edge (u,x) that lies in surface S; -1 if none.
  int triInSurface(int u, int x, int S) const
  {
    auto it = adj.find({std::min(u, x), std::max(u, x)});
    if (it == adj.end()) return -1;
    for (const int t : it->second)
      if (surfaceOf[t] == S) return t;
    return -1;
  }

  // In-plane direction at edge (u,x), perpendicular to the edge, pointing into
  // triangle t's interior (toward its third vertex).
  Vector3d perpInto(int u, int x, int t) const
  {
    const Vector3d d = (m.pos[x] - m.pos[u]).normalized();
    Vector3d p = m.tris[t].normal.cross(d).normalized();
    int w = -1;
    for (const int k : m.tris[t].v)
      if (k != u && k != x) w = k;
    if (w >= 0 && p.dot(m.pos[w] - m.pos[u]) < 0) p = -p;
    return p;
  }

  // The feature edges incident to vertex u that bound surface S (both incident
  // triangles considered), as neighbour-vertex ids. Selected and kept-sharp
  // edges alike: a kept edge is an offset line of zero setback, so the mitre
  // slides the inset along it rather than off it — which is what keeps a partly
  // selected surface (a brush, a one-sided sign filter) sewn to its sharp part.
  std::vector<int> boundaryEdgesOf(int u, int S) const
  {
    std::vector<int> nb;
    for (const auto& [key, ts] : adj) {
      if (key.first != u && key.second != u) continue;
      if (!feature.count(key)) continue;
      const int x = key.first == u ? key.second : key.first;
      for (const int t : ts)
        if (surfaceOf[t] == S) {
          nb.push_back(x);
          break;
        }
    }
    return nb;
  }

  // Where vertex u lands on surface S once its selected boundary edges are set
  // back: the mitre of the two offset lines. A kept-sharp boundary edge offsets
  // by zero, so its line passes through u and the mitre slides the inset along
  // it; two kept edges leave u put.
  Vector3d insetPoint(int u, int S) const
  {
    std::vector<int> nb = boundaryEdgesOf(u, S);
    if (nb.empty()) return m.pos[u];
    auto offsetLine = [&](int x, Vector3d& base, Vector3d& dir) {
      const int t = triInSurface(u, x, S);
      const EdgeKey e{std::min(u, x), std::max(u, x)};
      const double s = selected.count(e) ? setback(e) : 0.0;
      const Vector3d p = perpInto(u, x, t);
      base = m.pos[u] + s * p;
      dir = (m.pos[x] - m.pos[u]).normalized();
    };
    if (nb.size() == 1) {
      Vector3d base, dir;
      offsetLine(nb[0], base, dir);
      return base;
    }
    // With more than two feature edges bounding S at u (a non-disk patch corner),
    // mitre the two carrying the largest setback — the ones that actually move u.
    std::sort(nb.begin(), nb.end(), [&](int a, int c) {
      const double sa = selected.count({std::min(u, a), std::max(u, a)})
                          ? setback({std::min(u, a), std::max(u, a)}) : 0.0;
      const double sc = selected.count({std::min(u, c), std::max(u, c)})
                          ? setback({std::min(u, c), std::max(u, c)}) : 0.0;
      return sa > sc;
    });
    Vector3d b1, d1, b2, d2;
    offsetLine(nb[0], b1, d1);
    offsetLine(nb[1], b2, d2);
    const EdgeKey e0{std::min(u, nb[0]), std::max(u, nb[0])};
    const EdgeKey e1{std::min(u, nb[1]), std::max(u, nb[1])};
    return mitre(m.pos[u], b1, d1, selected.count(e0) ? setback(e0) : 0.0, b2, d2,
                 selected.count(e1) ? setback(e1) : 0.0);
  }

  // One local face-side of a crease at vertex u: the fan sector containing a
  // given incident triangle — the run of triangles reachable from it around u
  // without crossing a feature edge, bounded by the two feature edges at its
  // ends. This is the side a fillet arc actually seats against, and it is LOCAL:
  // it stays correct where a curved wall is split into per-facet surfaces (a
  // coarse cylinder, or the two walls of a tangent junction), because it reads
  // the feature edges rather than the surface id. `xb`/`xf` are the bounding
  // feature edges' far vertices; `tb`/`tf` the sector triangles incident to them
  // (for perpInto's orientation); `navg` the sector's averaged outward normal, so
  // adjacent edges on one smooth crease seat identically and their strips weld.
  struct Sector
  {
    int xb, xf, tb, tf;
    Vector3d navg;
  };
  std::optional<Sector> sectorOf(int u, int t) const
  {
    const std::vector<std::pair<int, int>> fan = fanAround(u);
    const int n = static_cast<int>(fan.size());
    if (n == 0) return std::nullopt;
    int i = -1;
    for (int k = 0; k < n; ++k)
      if (fan[k].first == t) { i = k; break; }
    if (i < 0) return std::nullopt;
    // A sector ends at a surface boundary — where the wall this arc seats against
    // ends. That is a superset of the feature edges (every crease is a boundary),
    // and it is what stops a sector at the sub-feature tangent gap of a junction,
    // so the strip caps there instead of the sector running on into the far wall.
    auto isBound = [&](int via) {
      const EdgeKey e{std::min(u, via), std::max(u, via)};
      auto it = adj.find(e);
      if (it == adj.end() || it->second.size() != 2) return true;
      return surfaceOf[it->second[0]] != surfaceOf[it->second[1]];
    };
    // fan[k].second is the edge (u, via) between triangle k and k+1.
    int f = i, steps = 0;
    while (!isBound(fan[f].second) && steps < n) { f = (f + 1) % n; ++steps; }
    if (steps >= n) return std::nullopt;  // no surface boundary in this fan
    int b = i;
    steps = 0;
    while (!isBound(fan[(b - 1 + n) % n].second) && steps < n) { b = (b - 1 + n) % n; ++steps; }
    Sector s;
    s.xf = fan[f].second;
    s.tf = fan[f].first;
    s.xb = fan[(b - 1 + n) % n].second;
    s.tb = fan[b].first;
    Vector3d nsum = Vector3d::Zero();
    for (int k = b;; k = (k + 1) % n) {
      nsum += m.tris[fan[k].first].normal;
      if (k == f) break;
    }
    s.navg = nsum.norm() > 1e-12 ? Vector3d(nsum.normalized()) : m.tris[t].normal;
    return s;
  }

  // Where vertex u lands once the crease is set back, on the side triangle t sits
  // on — the sector-local counterpart of insetPoint. Falls back to the
  // surface-based inset where the fan is not a clean manifold sector, so the
  // disk-patch majority (where sector and surface agree) is unchanged.
  Vector3d insetForTri(int u, int t) const
  {
    const auto cl = insetCollapse.find({u, t});
    if (cl != insetCollapse.end()) return cl->second;
    const auto ov = insetOverride.find({u, t});
    if (ov != insetOverride.end()) return ov->second;
    const auto s = sectorOf(u, t);
    if (!s) return insetPoint(u, surfaceOf[t]);
    auto offsetLine = [&](int x, int tside, Vector3d& base, Vector3d& dir) {
      const EdgeKey e{std::min(u, x), std::max(u, x)};
      const double sb = selected.count(e) ? setback(e) : 0.0;
      base = m.pos[u] + sb * perpInto(u, x, tside);
      dir = (m.pos[x] - m.pos[u]).normalized();
    };
    if (s->xb == s->xf) {
      Vector3d base, dir;
      offsetLine(s->xf, s->tf, base, dir);
      return base;
    }
    Vector3d b1, d1, b2, d2;
    offsetLine(s->xb, s->tb, b1, d1);
    offsetLine(s->xf, s->tf, b2, d2);
    const EdgeKey eb{std::min(u, s->xb), std::max(u, s->xb)};
    const EdgeKey ef{std::min(u, s->xf), std::max(u, s->xf)};
    return mitre(m.pos[u], b1, d1, selected.count(eb) ? setback(eb) : 0.0, b2, d2,
                 selected.count(ef) ? setback(ef) : 0.0);
  }

  // The far endpoint of the selected edge whose two incident triangles are t0/t1
  // (the vertex both share other than u); -1 if they do not share one.
  int farOf(int u, int t0, int t1) const
  {
    for (const int a : m.tris[t0].v)
      if (a != u)
        for (const int b : m.tris[t1].v)
          if (a == b) return a;
    return -1;
  }

  // At a mixed corner, edge (u,x)'s cross-section is seated one common distance
  // along the edge from u — the "pull-in station" — rather than at u itself. Each
  // face's own inset mitres the corner a different distance along the edge; seating
  // the section's two feet at those two distinct distances is exactly what twists
  // the first strip quad into the blade. Both feet share the deepest mitre's
  // station, so the section is one clean perpendicular slice (no twist). The full
  // deepest mitre is the furthest the strip must ever retreat (the planar clearance
  // from the neighbouring concave setback); the concave fillet recedes in 3D toward
  // the corner, so stationFrac pulls the slice back in from there and the beads flow
  // into the corner instead of necking a full radius short.
  //
  // rawStation is that seat: between the two face mitres, the deepest (smax) the full
  // un-blade station that retreats the strip a whole radius, the shallowest (smin) at or
  // behind the corner. A symmetric corner (the concave crease, both mitres equal) is a
  // no-op — it keeps its full mitre and does not overshoot. The along-sweep clamp keeps a
  // straight middle on a short corner-to-corner edge and holds crowded/degenerate corners
  // at their tiny station, so the tighter seat is a no-op there too.
  double rawStation(int u, int x) const
  {
    const Vector3d eh = (m.pos[x] - m.pos[u]).normalized();
    double smax = -1e30, smin = 1e30;
    for (const int t : adj.at(EdgeKey{std::min(u, x), std::max(u, x)})) {
      const double d = (insetForTri(u, t) - m.pos[u]).dot(eh);
      smax = std::max(smax, d);
      smin = std::min(smin, d);
    }
    return std::min(smin + stationFrac * (smax - smin),
                    kStationLenFrac * (m.pos[x] - m.pos[u]).norm());
  }

  double pullStation(int u, int x) const
  {
    const auto it = stationOverride.find({u, x});
    if (it == stationOverride.end()) return rawStation(u, x);
    // A corner override is the exact seat the surface the corner hands over to is built
    // on, so the straight-middle clamp does not apply to it — it would seat the strip
    // where that surface is not, and the corner would refuse. It still has to stop short
    // of what the far end takes, or the strip folds; on a raked corner the crease can be
    // split by a mesh vertex a couple of radii along, which is exactly that case.
    return std::min(it->second,
                    kStationFarClearFrac * ((m.pos[x] - m.pos[u]).norm() - rawStation(x, u)));
  }

  // Where edge (u,x)'s cross-section touches face t at u — the perpendicular foot
  // of the pulled-in section. Away from a mixed corner the caller uses insetForTri
  // instead; this is only the pulled seat that un-twists a mixed corner.
  Vector3d stripFoot(int u, int x, int t) const
  {
    const EdgeKey e{std::min(u, x), std::max(u, x)};
    const double sb = selected.count(e) ? setback(e) : 0.0;
    const Vector3d eh = (m.pos[x] - m.pos[u]).normalized();
    return m.pos[u] + pullStation(u, x) * eh + sb * perpInto(u, x, t);
  }

  // The blend cross-section at vertex u for one selected edge, its two sides given
  // by the edge's incident triangles t0/t1 — LOCAL, so it builds a proper
  // two-sided arc even where t0/t1's walls are split into per-facet surfaces. The
  // seat normal comes from each triangle's sector (averaged), so adjacent edges on
  // a smooth crease build the identical cross-section at u and their strips weld.
  // At a mixed corner the two feet are seated at one pulled-in station (stripFoot)
  // so the section is a clean perpendicular slice instead of a twisted blade.
  // The rolling-ball centre of edge (u)'s fillet arc for side triangles t0/t1 —
  // the point crossSectionEdge sweeps its arc about. Each side offers one: its
  // foot pushed one radius along its own sector normal (the plain rolling-ball
  // construction, so a planar crease builds the same arc a surface-based inset
  // would). Both feet sit on their side's offset line of the edge — exactly the
  // setback out from it — so the two candidates share their whole cross-sectional
  // position and differ only in how far along the edge each side's mitre carried
  // them. The centre is therefore one point at a choice of station, and the only
  // choice that reads the same on a crease and its mirror is the midpoint: it
  // seats the ball halfway between the two mitres. On a straight stretch the two
  // stations coincide and it is the one classical centre; at a corner it is the
  // one both sides agree on, and both feet come off it at the same distance, so
  // the section below is a circular arc rather than a radius-interpolating
  // spiral. Exposed so the mixed-corner saddle can read each ring point's
  // fillet-surface normal as (p - C): that normal is what lets the patch continue
  // the roll's own tangent instead of relaxing to a caving minimal surface.
  std::optional<Vector3d> filletCenter(int u, int t0, int t1, bool concave) const
  {
    const int x = pulled(u) ? farOf(u, t0, t1) : -1;
    const Vector3d Ta = x >= 0 ? stripFoot(u, x, t0) : insetForTri(u, t0);
    const Vector3d Tb = x >= 0 ? stripFoot(u, x, t1) : insetForTri(u, t1);
    const auto sa = sectorOf(u, t0);
    const auto sb = sectorOf(u, t1);
    if (!sa || !sb) return std::nullopt;
    Vector3d nA = sa->navg, nB = sb->navg;
    if (const auto it = normalOverride.find({u, t0}); it != normalOverride.end()) nA = it->second;
    if (const auto it = normalOverride.find({u, t1}); it != normalOverride.end()) nB = it->second;
    const double sgn = concave ? size : -size;
    return Vector3d(0.5 * ((Ta + sgn * nA) + (Tb + sgn * nB)));
  }

  std::vector<Vector3d> crossSectionEdge(int u, int t0, int t1, bool concave) const
  {
    const int x = pulled(u) ? farOf(u, t0, t1) : -1;
    const Vector3d Ta = x >= 0 ? stripFoot(u, x, t0) : insetForTri(u, t0);
    const Vector3d Tb = x >= 0 ? stripFoot(u, x, t1) : insetForTri(u, t1);
    if (isChamfer) return {Ta, Tb};
    // Same rolling-ball centre the saddle reads its ring normals from — with the
    // seat-normal override applied inside filletCenter, so a shared pass-through
    // builds one arc per side and the two strips weld.
    const auto Copt = filletCenter(u, t0, t1, concave);
    if (!Copt) return {Ta, Tb};
    const Vector3d C = *Copt;
    Vector3d ra = Ta - C, rb = Tb - C;
    const double la = ra.norm(), lb = rb.norm();
    if (la < 1e-9 || lb < 1e-9) return {Ta, Tb};
    ra /= la;
    rb /= lb;
    const double ang = std::acos(std::clamp(ra.dot(rb), -1.0, 1.0));
    if (ang < 1e-6) return {Ta, Tb};
    Vector3d axis = ra.cross(rb);
    if (axis.norm() < 1e-12) return {Ta, Tb};
    axis.normalize();
    std::vector<Vector3d> pts;
    pts.reserve(arcSegs + 1);
    for (int j = 0; j <= arcSegs; ++j) {
      const double a = ang * j / arcSegs;
      const Vector3d rot = ra * std::cos(a) + axis.cross(ra) * std::sin(a);
      const double rad = la + (lb - la) * j / arcSegs;
      pts.push_back(C + rad * rot);
    }
    // A convex roundover must stay inside the wedge of its own two faces: it
    // rounds the sharp edge off, so its surface never pokes past either face. At a
    // mixed corner the two face-insets are mitred to very different depths (a
    // convex edge sharing the vertex with a taller concave crease), which tilts
    // the arc plane so the bulge dips through a face and shows as a spike. Clamp
    // the interior arc points back onto the solid side of each face plane; the
    // tangent endpoints stay put (they are the insets the strips weld to), and a
    // concave arc is left alone — it bulges into the open valley by design.
    if (!concave) {
      const Vector3d nA0 = m.tris[t0].normal, nB0 = m.tris[t1].normal;
      for (size_t j = 1; j + 1 < pts.size(); ++j) {
        const double dA = nA0.dot(pts[j] - Ta);
        if (dA > 0) pts[j] -= dA * nA0;
        const double dB = nB0.dot(pts[j] - Tb);
        if (dB > 0) pts[j] -= dB * nB0;
      }
    }
    return pts;
  }

  // The strip along one selected edge: the cross-sections at its two ends,
  // stitched into quads.
  void emitEdge(const EdgeKey& e)
  {
    const auto& ts = adj.at(e);
    const bool concave = concaveOf.at(e);
    const std::vector<Vector3d> cu = crossSectionEdge(e.first, ts[0], ts[1], concave);
    const std::vector<Vector3d> cv = crossSectionEdge(e.second, ts[0], ts[1], concave);
    if (cu.size() != cv.size()) return;  // mismatched tessellation; skip (hole)
    std::vector<int> iu, iv;
    for (const auto& p : cu) iu.push_back(out.add(p));
    for (const auto& p : cv) iv.push_back(out.add(p));
    for (size_t j = 0; j + 1 < iu.size(); ++j) {
      out.tri(iu[j], iv[j], iv[j + 1]);
      out.tri(iu[j], iv[j + 1], iu[j + 1]);
    }
  }

  // The rotational order of the triangles incident to vertex u, each tagged with
  // its surface and the edge that leads to the next. Returns pairs (triangle,
  // outgoing neighbour vertex) walked around the fan; empty if u is not a clean
  // manifold fan.
  std::vector<std::pair<int, int>> fanAround(int u) const
  {
    // incident triangles
    std::vector<int> inc;
    for (size_t t = 0; t < m.tris.size(); ++t)
      for (const int k : m.tris[t].v)
        if (k == u) {
          inc.push_back(static_cast<int>(t));
          break;
        }
    if (inc.empty()) return {};
    std::vector<std::pair<int, int>> order;
    std::set<int> used;
    int cur = inc[0];
    // outgoing neighbour of cur at u: pick one of its two u-edges
    auto uEdges = [&](int t, int& x, int& y) {
      x = y = -1;
      for (const int k : m.tris[t].v)
        if (k != u) {
          if (x < 0) x = k;
          else y = k;
        }
    };
    int gx, gy;
    uEdges(cur, gx, gy);
    int nextVia = gy;
    for (size_t step = 0; step < inc.size(); ++step) {
      order.push_back({cur, nextVia});
      used.insert(cur);
      auto it = adj.find({std::min(u, nextVia), std::max(u, nextVia)});
      if (it == adj.end() || it->second.size() != 2) return {};
      const int nxt = it->second[0] == cur ? it->second[1] : it->second[0];
      int ax, ay;
      uEdges(nxt, ax, ay);
      nextVia = (ax == nextVia) ? ay : ax;
      cur = nxt;
      if (used.count(cur)) break;
    }
    return order;
  }

  // The corner patch at a vertex touched by selected edges: the ring of tangent
  // points and arc points around u, fan-filled. Single-signed corners come out
  // as a crude fan for now (a later pass seats them on the corner ball);
  // mixed-sign vertices are the deferred saddle and are filled crudely too.
  // The ball seated at a single-sign junction: at distance r from every face
  // incident to u, inside the material for a convex corner and out in the open
  // valley for a concave one — the "put a ball at the corner" idea, which
  // rounds a convex vertex off and fills a concave one. nullopt when the
  // incident faces cannot pin a centre (fewer than three independent normals).
  std::optional<Vector3d> cornerBall(int u, bool concave) const
  {
    std::vector<Vector3d> normals;
    for (size_t t = 0; t < m.tris.size(); ++t) {
      bool has = false;
      for (const int k : m.tris[t].v)
        if (k == u) has = true;
      if (!has) continue;
      const Vector3d n = m.tris[t].normal;
      bool dup = false;
      for (const auto& e : normals)
        if (e.dot(n) > kNormalDuplicateCos) dup = true;
      if (!dup) normals.push_back(n);
    }
    // Greedily pick three mutually independent normals.
    std::vector<Vector3d> basis;
    for (const auto& n : normals) {
      if (basis.empty()) { basis.push_back(n); continue; }
      if (basis.size() == 1) {
        if (basis[0].cross(n).norm() > kBasisIndependenceMin) basis.push_back(n);
        continue;
      }
      if (std::abs(basis[0].cross(basis[1]).dot(n)) > kBasisIndependenceMin) {
        basis.push_back(n);
        break;
      }
    }
    if (basis.size() < 3) return std::nullopt;
    Matrix3d N;
    Vector3d rhs;
    const double sgn = concave ? size : -size;  // inside (convex) vs valley (concave)
    for (int i = 0; i < 3; ++i) {
      N.row(i) = basis[i].transpose();
      rhs[i] = sgn;
    }
    if (std::abs(N.determinant()) < 1e-9) return std::nullopt;
    return Vector3d(m.pos[u] + N.inverse() * rhs);
  }

  // Tessellate the spherical corner cap bounded by `ring` onto the sphere of
  // radius r centred at C — the rounded corner surface itself, not a fan to one
  // apex. `poleUnit` is the unit direction from C to the cap's pole (the point on
  // the sphere the three arcs climb to): (u - C) for a convex vertex the ball
  // rounds off, and the same for a concave one, where it is the deepest point of
  // the filled valley. The boundary ring keeps its exact vertices (so the arc
  // strips stay welded); interior layers are slerped onto the sphere and the last
  // collapses to the pole. Winding is left to orient(). arcSegs layers match the
  // arc resolution the strips were built at.
  //
  // Concentric layers only tile the ring when the ring, seen from C, winds once
  // around the pole and never reaches it — the fan is a polar parameterisation and
  // that is its domain. A ring that touches the pole ray or doubles back in azimuth
  // (a sector whose mitre landed back on the vertex, an arc lying in a plane through
  // the pole) makes every layer slide over the one before it, and the cap covers
  // itself instead of the corner. Refuse there and let the caller close the ring
  // flat: a plane the corner does not round is worse to look at than a sphere, and
  // better than a surface folded inside out. Returns false when it emitted nothing.
  bool emitCap(const std::vector<int>& ring, const Vector3d& C, double r,
               const Vector3d& poleUnit)
  {
    const int n = static_cast<int>(ring.size());
    if (n < 3) return false;
    const int L = std::max(1, arcSegs);
    std::vector<Vector3d> u(n);
    for (int i = 0; i < n; ++i) {
      const Vector3d d = out.V[ring[i]] - C;
      const double len = d.norm();
      u[i] = len > 1e-12 ? Vector3d(d / len) : poleUnit;
    }
    if (!windsAboutPole(u, poleUnit)) return false;
    std::vector<std::vector<int>> layer(L + 1);
    layer[0] = ring;
    for (int k = 1; k < L; ++k) {
      layer[k].resize(n);
      const double t = static_cast<double>(k) / L;
      for (int i = 0; i < n; ++i)
        layer[k][i] = out.add(C + r * slerpUnit(u[i], poleUnit, t));
    }
    const int pole = out.add(C + r * poleUnit);
    for (int k = 0; k < L; ++k)
      for (int i = 0; i < n; ++i) {
        const int a = layer[k][i], b = layer[k][(i + 1) % n];
        if (k + 1 < L) {
          const int c = layer[k + 1][i], d = layer[k + 1][(i + 1) % n];
          out.tri(a, b, d);
          out.tri(a, d, c);
        } else {
          out.tri(a, b, pole);
        }
      }
    return true;
  }

  // Whether the ring directions `u` (unit, seen from the cap centre) go once round
  // `poleUnit` without ever reaching it: every azimuth step turns the same way and
  // they sum to a full turn. This is exactly what emitCap's concentric layers need
  // and nothing more, so a ring that merely bulges unevenly still fans.
  static bool windsAboutPole(const std::vector<Vector3d>& u, const Vector3d& poleUnit)
  {
    const int n = static_cast<int>(u.size());
    const Vector3d e1 = poleUnit.unitOrthogonal(), e2 = poleUnit.cross(e1);
    std::vector<double> az(n);
    for (int i = 0; i < n; ++i) {
      // A point on the pole ray has no azimuth, so the fan has no width there.
      if (std::abs(u[i].dot(poleUnit)) > std::cos(kCapPoleClearDeg * M_PI / 180.0)) return false;
      az[i] = std::atan2(u[i].dot(e2), u[i].dot(e1));
    }
    double total = 0.0;
    int sign = 0;
    for (int i = 0; i < n; ++i) {
      double d = az[(i + 1) % n] - az[i];
      while (d > M_PI) d -= 2 * M_PI;
      while (d < -M_PI) d += 2 * M_PI;
      const int s = d > 0 ? 1 : (d < 0 ? -1 : 0);
      if (s == 0) return false;
      if (sign == 0) sign = s;
      else if (s != sign) return false;
      total += d;
    }
    return std::abs(std::abs(total) - 2 * M_PI) < 1e-6;
  }

  // Tessellate a trihedral corner as a subdivided spherical triangle rather
  // than emitCap's pole-and-rings fan. A box corner — convex vertex rounded off or
  // concave valley filled in — is three fillet arcs meeting at three sector corners,
  // all on one sphere about C, so both signs are the same construction here. A
  // pole fan sweeps every boundary point to one shared apex, so its facets swing
  // toward that apex and shade as a whorl pinched at the centre with dark, recessed-
  // looking wedges where the arcs meet — the "corner bead". This instead lays a
  // barycentric grid over the three corner directions and projects it to the sphere:
  // no pole, and the grid lines run parallel to the three incident round-over strips
  // so the cap shades as an even continuation of them. The three boundary rows reuse
  // the exact ring vertices, so the cap welds to the strips even where the arcs are
  // not perfect great circles (a non-right-angle corner). `cornerPos` are the ring
  // indices of the three sector corners, in ring order. Returns false — and the
  // caller falls back to emitCap — unless the ring is exactly three equal-length
  // arcs, which is every real box/plate/pocket corner.
  bool emitCapTri(const std::vector<int>& ring, const std::vector<int>& cornerPos,
                  const Vector3d& C)
  {
    if (cornerPos.size() != 3) return false;
    const int n = static_cast<int>(ring.size());
    const int p0 = cornerPos[0], p1 = cornerPos[1], p2 = cornerPos[2];
    const int seg = p1 - p0;
    if (seg < 1 || p2 - p1 != seg || n - p2 != seg) return false;  // not three equal arcs
    auto dirOf = [&](int p) -> Vector3d {
      const Vector3d d = out.V[ring[p]] - C;
      const double l = d.norm();
      return l > 1e-12 ? Vector3d(d / l) : Vector3d(Vector3d::UnitZ());
    };
    // Radius = the rim's own mean distance to C, not a hard `size`. At a right-angle
    // corner the whole rim already lies on the size-sphere, so this is size exactly;
    // at an oblique corner, where the rim is not perfectly on the cornerBall sphere,
    // it seats the interior on the rim's radius and avoids an annular step between the
    // interior and the (exact) boundary vertices.
    double r = 0.0;
    for (int i = 0; i < n; ++i) r += (out.V[ring[i]] - C).norm();
    r /= n;
    const Vector3d u0 = dirOf(p0), u1 = dirOf(p1), u2 = dirOf(p2);
    // g[i][j], i + j <= seg: barycentric (seg-i-j, i, j)/seg over (u0, u1, u2). The
    // three boundaries map to the ring's own arc samples; interior points are the
    // barycentric direction renormalised onto the sphere.
    std::vector<std::vector<int>> g(seg + 1);
    for (int i = 0; i <= seg; ++i) {
      g[i].resize(seg + 1 - i);
      for (int j = 0; i + j <= seg; ++j) {
        if (j == 0)
          g[i][j] = ring[(p0 + i) % n];             // arc corner0 -> corner1
        else if (i == 0)
          g[i][j] = ring[(p0 - j + n) % n];         // arc corner0 -> corner2
        else if (i + j == seg)
          g[i][j] = ring[(p1 + j) % n];             // arc corner1 -> corner2
        else {
          const double w0 = static_cast<double>(seg - i - j) / seg,
                       w1 = static_cast<double>(i) / seg, w2 = static_cast<double>(j) / seg;
          Vector3d d = w0 * u0 + w1 * u1 + w2 * u2;
          const double l = d.norm();
          d = l > 1e-12 ? Vector3d(d / l) : u0;
          g[i][j] = out.add(C + r * d);
        }
      }
    }
    // Winding is left to orient(), exactly as emitCap leaves it.
    for (int i = 0; i < seg; ++i)
      for (int j = 0; i + j < seg; ++j) {
        out.tri(g[i][j], g[i + 1][j], g[i][j + 1]);
        if (i + j + 1 < seg) out.tri(g[i + 1][j], g[i + 1][j + 1], g[i][j + 1]);
      }
    return true;
  }

  // Ear-clip a ring in its best-fit plane, returning the triangles as LOCAL
  // indices (0..n-1) into the ring array rather than emitting them. The projection
  // is a simple polygon wherever the flat ear-clip already worked, so this base
  // triangulation is free of self-intersection — which is what makes it a safe
  // starting point for the faired saddle below. Returns false if the projection is
  // degenerate or no ear can be found.
  bool earClipRing(const std::vector<int>& ring, std::vector<std::array<int, 3>>& tris) const
  {
    const int n = static_cast<int>(ring.size());
    if (n < 3) return false;
    Vector3d c = Vector3d::Zero();
    for (const int i : ring) c += out.V[i];
    c /= n;
    Vector3d nrm = Vector3d::Zero();  // best-fit normal via Newell's method
    for (int i = 0; i < n; ++i)
      nrm += (out.V[ring[i]] - c).cross(out.V[ring[(i + 1) % n]] - c);
    if (nrm.norm() < 1e-12) return false;
    nrm.normalize();
    Vector3d ex = (std::abs(nrm.x()) < 0.9 ? Vector3d::UnitX() : Vector3d::UnitY());
    ex = (ex - ex.dot(nrm) * nrm).normalized();
    const Vector3d ey = nrm.cross(ex);
    std::vector<Vector2d> p(n);
    for (int i = 0; i < n; ++i) {
      const Vector3d d = out.V[ring[i]] - c;
      p[i] = {d.dot(ex), d.dot(ey)};
    }
    double area = 0;  // signed area to fix winding
    for (int i = 0; i < n; ++i) area += p[i].x() * p[(i + 1) % n].y() - p[(i + 1) % n].x() * p[i].y();
    std::vector<int> idx(n);
    for (int i = 0; i < n; ++i) idx[i] = (area < 0) ? (n - 1 - i) : i;
    auto cross2 = [](const Vector2d& a, const Vector2d& b, const Vector2d& cc) {
      return (b.x() - a.x()) * (cc.y() - a.y()) - (b.y() - a.y()) * (cc.x() - a.x());
    };
    std::vector<int> poly = idx;
    tris.clear();
    int guard = 0;
    while (poly.size() > 3 && guard++ < 4 * n) {
      const int m2 = static_cast<int>(poly.size());
      bool clipped = false;
      for (int i = 0; i < m2; ++i) {
        const int ia = poly[(i + m2 - 1) % m2], ib = poly[i], ic = poly[(i + 1) % m2];
        const Vector2d &A = p[ia], &B = p[ib], &C2 = p[ic];
        if (cross2(A, B, C2) <= 1e-12) continue;  // reflex or collinear
        bool ear = true;
        for (int j = 0; j < m2; ++j) {
          const int k = poly[j];
          if (k == ia || k == ib || k == ic) continue;
          const Vector2d& P = p[k];
          if (cross2(A, B, P) >= 0 && cross2(B, C2, P) >= 0 && cross2(C2, A, P) >= 0) {
            ear = false;
            break;
          }
        }
        if (!ear) continue;
        tris.push_back({ia, ib, ic});
        poly.erase(poly.begin() + i);
        clipped = true;
        break;
      }
      if (!clipped) return false;
    }
    if (poly.size() == 3) tris.push_back({poly[0], poly[1], poly[2]});
    return true;
  }

  // Ear-clip a planar polygon given by position (not by out.V index) and emit its
  // triangles. Used by the surface pass to re-triangulate a face corner cut back
  // at a mixed vertex: the polygon is the inset triangle with the corner replaced
  // by its perpendicular feet and mitre, all coplanar with the face, so a plain
  // in-plane ear-clip triangulates it. Returns false (caller falls back to a plain
  // inset triangle) if the projection is degenerate or no ear is found.
  bool fillPlanarPolygon(const std::vector<Vector3d>& poly)
  {
    const int n = static_cast<int>(poly.size());
    if (n < 3) return false;
    Vector3d c = Vector3d::Zero();
    for (const auto& v : poly) c += v;
    c /= n;
    Vector3d nrm = Vector3d::Zero();
    for (int i = 0; i < n; ++i) nrm += (poly[i] - c).cross(poly[(i + 1) % n] - c);
    if (nrm.norm() < 1e-15) return false;
    nrm.normalize();
    Vector3d ex = (std::abs(nrm.x()) < 0.9 ? Vector3d::UnitX() : Vector3d::UnitY());
    ex = (ex - ex.dot(nrm) * nrm).normalized();
    const Vector3d ey = nrm.cross(ex);
    std::vector<Vector2d> p(n);
    for (int i = 0; i < n; ++i) p[i] = {(poly[i] - c).dot(ex), (poly[i] - c).dot(ey)};
    double area = 0;
    for (int i = 0; i < n; ++i)
      area += p[i].x() * p[(i + 1) % n].y() - p[(i + 1) % n].x() * p[i].y();
    std::vector<int> idx(n);
    for (int i = 0; i < n; ++i) idx[i] = (area < 0) ? (n - 1 - i) : i;
    auto cross2 = [](const Vector2d& a, const Vector2d& b, const Vector2d& cc) {
      return (b.x() - a.x()) * (cc.y() - a.y()) - (b.y() - a.y()) * (cc.x() - a.x());
    };
    std::vector<int> ring = idx;
    std::vector<std::array<int, 3>> tris;
    int guard = 0;
    while (ring.size() > 3 && guard++ < 4 * n) {
      const int m2 = static_cast<int>(ring.size());
      bool clipped = false;
      for (int i = 0; i < m2; ++i) {
        const int ia = ring[(i + m2 - 1) % m2], ib = ring[i], ic = ring[(i + 1) % m2];
        if (cross2(p[ia], p[ib], p[ic]) <= 1e-12) continue;
        bool ear = true;
        for (int j = 0; j < m2; ++j) {
          const int k = ring[j];
          if (k == ia || k == ib || k == ic) continue;
          if (cross2(p[ia], p[ib], p[k]) >= 0 && cross2(p[ib], p[ic], p[k]) >= 0 &&
              cross2(p[ic], p[ia], p[k]) >= 0) {
            ear = false;
            break;
          }
        }
        if (!ear) continue;
        tris.push_back({ia, ib, ic});
        ring.erase(ring.begin() + i);
        clipped = true;
        break;
      }
      if (!clipped) return false;
    }
    if (ring.size() == 3) tris.push_back({ring[0], ring[1], ring[2]});
    for (const auto& t : tris) out.tri(poly[t[0]], poly[t[1]], poly[t[2]]);
    return true;
  }

  // The ring one surface triangle lands on, re-triangulated where it meets a mixed
  // corner. Away from a mixed corner it is just the three inset vertices
  // (unchanged). At a mixed corner u the strip along a selected edge (u,x) is
  // pulled in to a common station (stripFoot), which lands past that face's mitre
  // on the shorter-mitre face; the face must carry the strip's foot as a boundary
  // vertex there, or the strip's tangent point T-junctions the mitre and opens a
  // hole. Each such foot goes on the inset edge between the corner's mitre and x's
  // inset, in order from the corner outward — but only where it actually falls
  // inside that edge, see below.
  // The pulled-in strip foot edge (a,b) leaves on face ti, where that foot belongs
  // to the face at all. It only does where it falls strictly inside the inset edge
  // it is being inserted into: on the furthest-mitre face it lands on the mitre and
  // adds nothing; where the pull-in station outruns the inset edge it lands at or
  // beyond one of its ends, and inserting it there would spike the ring back on
  // itself — a self-overlapping polygon no triangulation can save. Both the
  // per-triangle pass and the whole-face footprint read it, so the two agree on the
  // face boundary vertex for vertex, whichever emits.
  std::optional<Vector3d> footOn(int a, int b, int ti) const
  {
    if (!pulled(a) || !isSelected(a, b)) return std::nullopt;
    const Vector3d f = stripFoot(a, b, ti);
    const Vector3d A = insetForTri(a, ti), d = insetForTri(b, ti) - A;
    const double dd = d.squaredNorm();
    if (dd < 1e-18) return std::nullopt;
    const double s = (f - A).dot(d) / dd;
    if (s < 1e-9 || s > 1.0 - 1e-9) return std::nullopt;
    return f;
  }

  std::vector<Vector3d> surfacePolygon(int ti) const
  {
    const auto& v = m.tris[ti].v;
    const std::array<Vector3d, 3> P{insetForTri(v[0], ti), insetForTri(v[1], ti),
                                    insetForTri(v[2], ti)};
    bool anyCorner = false;
    for (const int k : v)
      if (pulled(k)) anyCorner = true;
    if (!anyCorner) return {P[0], P[1], P[2]};
    // Walk the three directed edges, emitting each tail vertex's inset then any
    // strip feet that fall on that edge — near-tail first, near-head second.
    std::vector<Vector3d> poly;
    auto footOn = [&](int a, int b) { return this->footOn(a, b, ti); };
    for (int i = 0; i < 3; ++i) {
      const int a = v[i], b = v[(i + 1) % 3];
      poly.push_back(P[i]);
      if (auto fa = footOn(a, b)) poly.push_back(*fa);        // near tail a
      if (auto fb = footOn(b, a)) poly.push_back(*fb);        // near head b
    }
    return poly;
  }

  // Emit that ring: the plain inset triangle, or an in-plane ear-clip of the
  // corner-cut polygon, falling back to the plain triangle if the clip refuses.
  void emitSurfaceTri(int ti)
  {
    if (const auto it = surfaceOverride.find(ti); it != surfaceOverride.end()) {
      out.tri(it->second[0], it->second[1], it->second[2]);
      return;
    }
    const std::vector<Vector3d> poly = surfacePolygon(ti);
    if (poly.size() == 3) {
      out.tri(poly[0], poly[1], poly[2]);
      return;
    }
    if (!fillPlanarPolygon(poly)) {
      const auto& v = m.tris[ti].v;
      out.tri(insetForTri(v[0], ti), insetForTri(v[1], ti), insetForTri(v[2], ti));
    }
  }

  // --- the surface's own triangulation --------------------------------------

  // The triangles of each smooth surface, by surface id.
  std::map<int, std::vector<int>> surfaceGroups() const
  {
    std::map<int, std::vector<int>> g;
    for (size_t t = 0; t < m.tris.size(); ++t) g[surfaceOf[t]].push_back(static_cast<int>(t));
    return g;
  }

  // A surface is planar when every one of its triangles carries the same normal.
  // Only there is "which side of the face is this triangle on" a question a signed
  // area answers, and only there is one diagonal as good as another.
  bool surfacePlanar(const std::vector<int>& tris, Vector3d& nrm) const
  {
    nrm = m.tris[tris.front()].normal;
    for (const int t : tris)
      if (m.tris[t].normal.dot(nrm) < 1 - 1e-12) return false;
    return true;
  }

  // The maximal coplanar runs of one surface, each with its own normal, in the
  // triangles' own order (so the split is deterministic). A surface is grouped by
  // near-tangency, so where a cylinder runs tangentially into another face the two
  // walls join one surface that is not planar as a whole while each wall still is.
  // Everything that reasons about a face plane wants those pieces, not a verdict on
  // the union: on a planar surface this is one run and the caller is unchanged.
  std::vector<std::pair<Vector3d, std::vector<int>>> coplanarRuns(
      const std::vector<int>& tris) const
  {
    std::vector<std::pair<Vector3d, std::vector<int>>> runs;
    for (const int t : tris) {
      const Vector3d n = m.tris[t].normal;
      bool placed = false;
      for (auto& [rn, rt] : runs)
        if (rn.dot(n) >= 1 - 1e-12) {
          rt.push_back(t);
          placed = true;
          break;
        }
      if (!placed) runs.push_back({n, {t}});
    }
    return runs;
  }

  // --- the face footprint ---------------------------------------------------

  // One directed boundary link of a surface: the edge a->b as triangle t carries it,
  // t being the surface's own side of it. Chained, these are the loops that bound
  // the face, wound the way t is wound — so the outer loop turns counter-clockwise
  // about the face normal and every hole turns the other way, which is what lets a
  // signed area tell the two apart.
  struct BLink
  {
    int a, b, t;
  };

  // The boundary loops of one surface, in order. Empty if the boundary is not a
  // disjoint set of simple cycles — a vertex the surface reaches twice, an edge the
  // mesh does not carry with exactly two triangles — because then "the region this
  // face covers" is not one polygon and the footprint pass has nothing to trim.
  std::vector<std::vector<BLink>> surfaceLoops(const std::vector<int>& tris) const
  {
    const std::set<int> inS(tris.begin(), tris.end());
    std::map<int, BLink> next;
    for (const int t : tris)
      for (int i = 0; i < 3; ++i) {
        const int a = m.tris[t].v[i], b = m.tris[t].v[(i + 1) % 3];
        const auto it = adj.find({std::min(a, b), std::max(a, b)});
        if (it == adj.end() || it->second.size() != 2) return {};
        const int other = it->second[0] == t ? it->second[1] : it->second[0];
        if (inS.count(other)) continue;  // an interior seam, not a boundary of the face
        if (!next.emplace(a, BLink{a, b, t}).second) return {};
      }
    if (next.empty()) return {};
    std::vector<std::vector<BLink>> loops;
    std::set<int> used;
    for (const auto& [start, first] : next) {
      if (used.count(start)) continue;
      std::vector<BLink> loop;
      int at = start;
      while (true) {
        const auto it = next.find(at);
        if (it == next.end()) return {};
        if (!used.insert(at).second) return {};
        loop.push_back(it->second);
        at = it->second.b;
        if (at == start) break;
        if (loop.size() > next.size()) return {};
      }
      if (loop.size() < 3) return {};
      loops.push_back(std::move(loop));
    }
    return used.size() == next.size() ? loops : std::vector<std::vector<BLink>>{};
  }

  // A boundary loop's seats: one point per link, where the link's tail vertex lands
  // on this face. This is the polygon the trim looks for crossings in — the strip
  // feet are deliberately left out of it, because a foot is not a seat of its own
  // (it is carried by the two seats it sits between) and a trim that collapses those
  // two drops the foot with them.
  std::vector<Vector3d> loopSeats(const std::vector<BLink>& loop) const
  {
    std::vector<Vector3d> p;
    p.reserve(loop.size());
    for (const BLink& l : loop) p.push_back(insetForTri(l.a, l.t));
    return p;
  }

  // The same loop as the face actually carries it: seats, plus the pulled-in strip
  // feet that fall on each link, in the order the per-triangle pass inserts them.
  // Consecutive points closer than the weld are one seat and are emitted once.
  std::vector<Vector3d> loopFootprint(const std::vector<BLink>& loop) const
  {
    std::vector<Vector3d> p;
    const double weld = kFootprintSeatTol;
    auto push = [&](const Vector3d& q) {
      if (!p.empty() && (p.back() - q).norm() < weld) return;
      p.push_back(q);
    };
    for (const BLink& l : loop) {
      push(insetForTri(l.a, l.t));
      if (const auto fa = footOn(l.a, l.b, l.t)) push(*fa);
      if (const auto fb = footOn(l.b, l.a, l.t)) push(*fb);
    }
    while (p.size() > 1 && (p.front() - p.back()).norm() < weld) p.pop_back();
    return p;
  }

  // Trim a face's boundary where the retreat has turned it back on itself.
  //
  // Every round-over along a face's boundary pulls that boundary in by its own
  // setback. Where two of them are closer together than the room between them — the
  // waist of two merged bosses, the notch of a peanut-shaped hole, a shallow corner
  // whose mitre outruns its own link — the retreated boundary crosses itself, and
  // the piece beyond the crossing is a region the face does not cover: it is walked
  // the wrong way round, so it is emitted inside-out, lying in the face plane on top
  // of its neighbours as an exact 180 deg fold pair. That stays edge-manifold and so
  // passes every gate while shading as a bright dart.
  //
  // The trim is to seat the whole swallowed run on the crossing point. Which run is
  // swallowed needs no threshold to decide: a crossing splits the loop in two, the
  // two signed areas sum to the loop's own, and exactly one of them can therefore
  // turn against it. That one is the fold-back. Collapsing it moves seats rather
  // than dropping points, so the strips, kept seams and corner patches that read
  // those seats follow the face instead of being left behind by it — and only the
  // seats on THIS face move, so each face is trimmed to its own footprint while the
  // same vertex keeps its own seat on every other face it belongs to.
  //
  // One crossing can expose the next, so the sweep repeats.
  //
  // A WHOLE SURFACE, not a coplanar run of one: the trim moves seats, and a seat on
  // the seam between two runs of one surface is carried by both. Moving it for one
  // run only tears the seam open; moving it for both puts one run's seat off its own
  // plane. So the trim stays where every seat it touches is the surface's own.
  void trimFootprintSpikes()
  {
    std::vector<std::vector<int>> vTris(m.pos.size());
    for (size_t t = 0; t < m.tris.size(); ++t)
      for (const int u : m.tris[t].v) vTris[u].push_back(static_cast<int>(t));

    for (int sweep = 0; sweep < kFoldRepairSweeps; ++sweep) {
      bool cut = false;
      for (const auto& [S, tris] : surfaceGroups()) {
        Vector3d nrm;
        if (tris.size() < 2 || !surfacePlanar(tris, nrm)) continue;
        for (const auto& loop : surfaceLoops(tris)) {
          const std::vector<Vector3d> seat = loopSeats(loop);
          const int n = static_cast<int>(seat.size());
          if (n < 4) continue;
          const Basis2 B = planeBasis(seat[0], nrm);
          std::vector<Vector2d> p(n);
          for (int i = 0; i < n; ++i) p[i] = project2(B, seat[i]);
          const double eps = kFootprintEpsFrac * size;
          const double whole = loopArea2(p);
          if (std::abs(whole) < eps * eps) continue;

          bool done = false;
          for (int i = 0; i < n && !done; ++i)
            for (int j = i + 2; j < n && !done; ++j) {
              if (i == 0 && j == n - 1) continue;  // adjacent across the seam
              Vector2d X;
              if (!segCross2(p[i], p[(i + 1) % n], p[j], p[(j + 1) % n], eps, X)) continue;
              // The run i+1..j, closed through the crossing. Against it stands the
              // rest of the loop, also closed through the crossing; their areas sum
              // to the loop's, so at most one turns the other way.
              std::vector<Vector2d> run{X};
              for (int k = i + 1; k <= j; ++k) run.push_back(p[k]);
              const double a = loopArea2(run);
              if (a * whole >= 0) continue;  // this run is not the fold-back
              // Every seat the run gives up has to be near the crossing that
              // swallows it, and the same few setbacks the link repair allows its
              // survivor is the bound. It is two tests in one. A crossing that has
              // raced off down the boundary is an artefact of two near-parallel
              // offset lines rather than a seat, and collapsing onto it would
              // teleport a strip across the solid. And a run that reaches far from
              // its crossing is not a spike at all but a whole region the retreat
              // has turned over — an arm end face narrower than its two round-overs
              // — which has no single point to collapse to: seating it on one
              // sweeps every strip along it into the same point and makes a worse
              // fold than the one it set out to trim. That case is the size regime,
              // not this trim, and it is left for the strips to answer for.
              const Vector3d X3 = unproject2(B, X);
              bool spike = true;
              for (int k = i + 1; k <= j && spike; ++k)
                spike = (X3 - seat[k]).norm() <= kFoldSurvivorSetbacks * size;
              if (!spike) continue;
              for (int k = i + 1; k <= j; ++k) {
                const int u = loop[k].a;
                const Vector3d at = seat[k];
                if ((X3 - at).squaredNorm() < 1e-24) continue;
                for (const int tt : vTris[u])
                  if (surfaceOf[tt] == S && (insetForTri(u, tt) - at).squaredNorm() < 1e-18)
                    insetCollapse[{u, tt}] = X3;
              }
              cut = done = true;
            }
        }
      }
      if (!cut) return;
    }
  }

  // --- the size gate --------------------------------------------------------

  // Add one selected crease to the refusal set, together with the rest of the
  // straight run it was cut from. `inc` is vertex -> selected neighbours.
  void refuseRun(int a, int b, const std::map<int, std::vector<int>>& inc,
                 std::set<EdgeKey>& bad) const
  {
    const EdgeKey seed{std::min(a, b), std::max(a, b)};
    if (!selected.count(seed) || !bad.insert(seed).second) return;
    const double cosRun = std::cos(kOverRoundRunDeg * M_PI / 180.0);
    for (int back = 0; back < 2; ++back) {
      int prev = back ? b : a, at = back ? a : b;
      while (true) {
        const auto it = inc.find(at);
        if (it == inc.end()) break;
        const Vector3d d = (m.pos[at] - m.pos[prev]).normalized();
        int nxt = -1;
        double bestDot = cosRun;
        for (const int w : it->second) {
          if (w == prev) continue;
          const double c = (m.pos[w] - m.pos[at]).normalized().dot(d);
          if (c > bestDot) { bestDot = c; nxt = w; }
        }
        if (nxt < 0) break;
        if (!bad.insert({std::min(at, nxt), std::max(at, nxt)}).second) break;
        prev = at;
        at = nxt;
      }
    }
  }

  // The creases the size does not fit, as the faces they stand on report it.
  //
  // A boundary link retreats parallel to itself, so the only thing the retreat can
  // do to it is shorten it: the seats at its two ends walk toward each other by the
  // setbacks of the creases meeting there. Where those two setbacks are more than
  // the link is long the seats cross and the link comes back pointing the other way
  // — no strip of face is left between the two round-overs and both are asking for
  // the same material. A whole loop can go that way at once, and then its retreated
  // area has flipped sign or vanished rather than shrunk: the face is gone, not
  // narrowed. Neither is a shape any blend of this size can carry.
  //
  // Neither is a trim, either, which is the line between this and
  // trimFootprintSpikes: a spike has a crossing point to collapse the swallowed run
  // onto, while a region that has turned over has no single point to seat on, and
  // seating it on one sweeps every strip along it together and folds worse than
  // leaving it. That case is refused here instead.
  //
  // What is refused is not the reversed link but the two creases that ate it — the
  // links either side of it, whose setbacks it stands between — and with each of
  // them the rest of the straight run it belongs to.
  //
  // Both tests are exact: a link either kept its direction or it did not, and a loop
  // either kept its turn or it did not. The only tolerance is the footprint's own
  // degeneracy floor, which decides when a length or an area is rounding rather than
  // geometry.
  std::set<EdgeKey> overRoundEdges() const
  {
    const double eps = kFootprintEpsFrac * size;
    std::map<int, std::vector<int>> inc;
    for (const auto& e : selected) {
      inc[e.first].push_back(e.second);
      inc[e.second].push_back(e.first);
    }
    std::set<EdgeKey> bad;
    // Each planar surface's source outline: its plane, and the loops that bound it
    // projected into one basis for the whole surface, each with its signed area — so
    // a point can be asked whether it stands on the face.
    struct Outline
    {
      Basis2 B;
      Vector3d origin, nrm;
      std::vector<std::pair<std::vector<Vector2d>, double>> loops;
    };
    std::map<int, Outline> faceOutline;
    for (const auto& [S, tris] : surfaceGroups()) {
      Vector3d fn;
      if (tris.size() < 2 || !surfacePlanar(tris, fn)) continue;
      Outline o;
      o.origin = m.pos[m.tris[tris.front()].v[0]];
      o.nrm = fn;
      o.B = planeBasis(o.origin, fn);
      for (const auto& loop : surfaceLoops(tris)) {
        if (loop.size() < 3) continue;
        std::vector<Vector2d> poly;
        poly.reserve(loop.size());
        for (const auto& l : loop) poly.push_back(project2(o.B, m.pos[l.a]));
        const double a = loopArea2(poly);
        if (std::abs(a) < eps * eps) continue;
        o.loops.emplace_back(std::move(poly), a);
      }
      if (!o.loops.empty()) faceOutline.emplace(S, std::move(o));
    }
    for (const auto& [S, tris] : surfaceGroups()) {
      Vector3d nrm;
      if (tris.size() < 2 || !surfacePlanar(tris, nrm)) continue;
      for (const auto& loop : surfaceLoops(tris)) {
        const int n = static_cast<int>(loop.size());
        if (n < 3) continue;
        const Basis2 B = planeBasis(m.pos[loop[0].a], nrm);
        std::vector<Vector2d> src(n), ret(n);
        std::vector<Vector3d> seat(n);
        for (int i = 0; i < n; ++i) {
          seat[i] = insetForTri(loop[i].a, loop[i].t);
          src[i] = project2(B, m.pos[loop[i].a]);
          ret[i] = project2(B, seat[i]);
        }
        const double a0 = loopArea2(src);
        if (std::abs(a0) < eps * eps) continue;  // no source loop to speak of
        // The loop turns with the face (outer) or against it (a hole); either way
        // the retreat may only shrink it, never turn it the other way.
        const double a1 = (a0 > 0 ? 1.0 : -1.0) * loopArea2(ret);
        const bool gone = a1 <= eps * eps;  // the face has no interior left at all
        for (int i = 0; i < n; ++i) {
          const Vector3d d = m.pos[loop[i].b] - m.pos[loop[i].a];
          if (d.norm() < eps) continue;
          const double kept = (seat[(i + 1) % n] - seat[i]).dot(d.normalized());
          // A link the retreat has merely closed, on a face that still has an
          // interior, is a trim's business rather than the gate's: a collapsed spike
          // seats both ends of a link on one crossing point without consuming the
          // face. A link that has turned right round has no such reading.
          if (!(kept < -eps || (gone && kept <= eps))) continue;
          const auto& p = loop[(i - 1 + n) % n];
          const auto& q = loop[(i + 1) % n];
          refuseRun(p.a, p.b, inc, bad);
          refuseRun(q.a, q.b, inc, bad);
        }
      }
    }
    // A seat is a point ON its face. The retreat only ever carries a boundary vertex
    // into the face, so a seat outside the face's own source outline is not a seat at
    // all, and the strip foot that reads it is placed off the surface it is supposed
    // to be tangent to.
    //
    // It happens where a refused crease hands over to a selected one leaving the
    // vertex almost the same way. The kept edge offsets by zero, so the two offset
    // lines cross 1/sin(turn) setbacks out; on a tessellated arc, where the turn is a
    // single facet, that is several setbacks down the boundary and past the end of a
    // face that ends there. The mitre limit holds the slide finite, not on the face.
    //
    // A slide that stays on the face is exactly what the kept edge is there for — the
    // seat runs along the sharp edge and the sharp part stays sewn to the blended
    // part — so what is asked is membership of the SOURCE outline, which the retreat
    // can only shrink, with kSeatEscapeSizes of slack for the facet-to-facet spill a
    // tessellated wall makes of one surface per facet. It is asked of the seats the
    // strips actually stand on, per crease end and per side, because the trim seats
    // per (vertex, triangle): one facet at a vertex can be pulled back onto a crossing
    // while the next one along keeps the racing mitre.
    for (const auto& e : selected) {
      bool off = false;
      for (const int u : {e.first, e.second})
        for (const int t : adj.at(e)) {
          const auto it = faceOutline.find(surfaceOf[t]);
          if (it == faceOutline.end()) continue;
          const Outline& o = it->second;
          const Vector3d P = insetForTri(u, t);
          // Only a seat that is IN the face's plane and outside its outline. A seat
          // that has left the plane altogether is a different fault with a different
          // answer — a fan run stamped with one shared inset across a smooth junction
          // of two planes — and refusing the crease is not that one's remedy.
          if (std::abs(o.nrm.dot(P - o.origin)) > eps) continue;
          const Vector2d q = project2(o.B, P);
          for (const auto& [poly, area] : o.loops)
            if (area > 0) off = off || loopEscape(poly, q) > kSeatEscapeSizes * size;
        }
      if (off) refuseRun(e.first, e.second, inc, bad);
    }
    return bad;
  }

  // Emit one planar face as its whole footprint rather than triangle by triangle.
  //
  // The per-triangle pass moves each vertex to its own seat and keeps the source
  // triangulation. That is only faithful while the seats move little enough for the
  // old diagonals to still fit the region they now bound: a face whose boundary
  // retreats unevenly — an L whose reflex corner mitres inward while its far side
  // stays put — carries diagonals that end up on the wrong side of their own quad,
  // and those triangles come back inside-out, again as a fold lying in the face.
  // The footprint owes nothing to the source triangulation: the boundary is the
  // face's own retreated loops and the interior is re-cut to fit them.
  //
  // The boundary vertices are exactly the ones the per-triangle pass would have
  // emitted, in the same order, so every strip, kept seam and corner patch welds to
  // this face exactly as before — only the interior diagonals differ. Anything the
  // pass cannot vouch for is refused (returns false) and the caller emits the face
  // the old way: a face whose loops will not chain, a boundary still crossing itself
  // after the trim, an ear clip that finds no ear, a triangulation whose area does
  // not match its own polygon's.
  bool emitSurfacePatch(const std::vector<int>& tris)
  {
    Vector3d nrm;
    if (tris.size() < 2 || !surfacePlanar(tris, nrm)) return false;
    const auto loops = surfaceLoops(tris);
    if (loops.empty()) return false;

    std::vector<std::vector<Vector3d>> ring3;
    for (const auto& loop : loops) {
      std::vector<Vector3d> f = loopFootprint(loop);
      if (f.size() >= 3) ring3.push_back(std::move(f));
    }
    if (ring3.empty()) return false;

    const Basis2 B = planeBasis(ring3.front().front(), nrm);
    const double eps = kFootprintEpsFrac * size;
    std::vector<Vector3d> P3;
    std::vector<Vector2d> P2;
    std::vector<std::vector<int>> ring;
    for (const auto& f : ring3) {
      std::vector<int> idx;
      for (const Vector3d& q : f) {
        idx.push_back(static_cast<int>(P3.size()));
        P3.push_back(q);
        P2.push_back(project2(B, q));
      }
      ring.push_back(std::move(idx));
    }

    // No loop may cross itself or any other, or the region they bound is not the
    // one they enclose and no triangulation of it means anything.
    for (size_t la = 0; la < ring.size(); ++la)
      for (size_t lb = la; lb < ring.size(); ++lb)
        for (size_t i = 0; i < ring[la].size(); ++i)
          for (size_t j = (la == lb ? i + 1 : 0); j < ring[lb].size(); ++j) {
            Vector2d X;
            if (segCross2(P2[ring[la][i]], P2[ring[la][(i + 1) % ring[la].size()]],
                          P2[ring[lb][j]], P2[ring[lb][(j + 1) % ring[lb].size()]], eps, X))
              return false;
          }

    // The outer loop is the one that turns with the face; every other is a hole and
    // must turn against it. A face whose largest loop turns the wrong way is not a
    // footprint this pass understands.
    std::vector<double> area(ring.size());
    size_t outer = 0;
    for (size_t k = 0; k < ring.size(); ++k) {
      std::vector<Vector2d> q;
      for (const int i : ring[k]) q.push_back(P2[i]);
      area[k] = loopArea2(q);
      if (std::abs(area[k]) > std::abs(area[outer])) outer = k;
    }
    if (area[outer] <= 0) return false;
    double want = area[outer];
    for (size_t k = 0; k < ring.size(); ++k)
      if (k != outer) {
        if (area[k] >= 0) return false;
        want += area[k];
      }
    if (want <= eps * eps) return false;

    std::vector<int> merged = ring[outer];
    for (size_t k = 0; k < ring.size(); ++k)
      if (k != outer && !bridgeHole(merged, ring[k], P2, eps)) return false;

    std::vector<std::array<int, 3>> tri2;
    if (!earClip2(P2, merged, tri2, eps)) return false;
    double got = 0;
    for (const auto& t : tri2) {
      const double a = cross2(P2[t[0]], P2[t[1]], P2[t[2]]);
      if (a <= 0) return false;  // a bridge crossed something: the patch laps over itself
      got += a;
    }
    if (std::abs(got - want) > kFootprintAreaTol * want) return false;

    for (const auto& t : tri2) out.tri(P3[t[0]], P3[t[1]], P3[t[2]]);
    return true;
  }

  // Where the inset turns a face triangle inside out and the surface's boundary is
  // not what crossed, the fault is in the diagonal rather than in the points: two
  // triangles sharing an interior seam of the source triangulation have been
  // carried to mitres that put the seam on the wrong side of the quad they span,
  // and the pair comes back as a fold lying in the face. Flipping the seam costs
  // nothing — it moves no point, so every strip, kept seam and corner patch still
  // seats exactly where it did and the surface keeps the same boundary edges — and
  // it takes both triangles the right way round again.
  //
  // Only a seam interior to one coplanar run of a surface is eligible, and only a
  // flip that leaves fewer triangles turned over is taken, so the pass either
  // improves the face or does nothing to it. A boundary link that has reversed is a
  // different fault, in the offset itself rather than in the triangulation, and this
  // does not touch it.
  //
  // A RUN, NOT THE WHOLE SURFACE. Where a cylinder meets a face tangentially the two
  // walls group into one surface that is not planar, and gating on the surface left
  // exactly those faces unrepaired. That is where the repair is needed most: with no
  // model vertex on the tangency line — an odd $fn puts it mid-facet — the union
  // leaves razor needles along the facet whose corners then set back a whole radius,
  // which turns them over and laps the retreated face over itself.
  //
  // Its reach is now the faces emitSurfacePatch declines — a face re-cut from its
  // own footprint has no source diagonal left to get wrong. Those are the crowded
  // ones, where a whole region of the face has turned over rather than one quad,
  // and there it still repairs what it can: dropping this pass leaves the corpus
  // unchanged but costs two of the collision probes their clean count.
  void flipInvertedInsets()
  {
    for (const auto& [S, all] : surfaceGroups())
     for (const auto& run : coplanarRuns(all)) {
      const Vector3d& nrm = run.first;
      const std::vector<int>& tris = run.second;
      if (tris.size() < 2) continue;

      // One inset per vertex of this surface. A vertex the surface reaches through
      // two separate fans has two, and no single answer to seat a flipped triangle
      // on; it is left out, along with the mixed corners whose face triangles are
      // re-triangulated round the pulled-in strip feet.
      std::map<int, Vector3d> at;
      std::set<int> barred;
      for (const int t : tris)
        for (const int u : m.tris[t].v) {
          const Vector3d p = insetForTri(u, t);
          const auto [it, ins] = at.emplace(u, p);
          if (!ins && (it->second - p).squaredNorm() > 1e-24) barred.insert(u);
          if (pulled(u)) barred.insert(u);
        }

      std::map<int, std::array<int, 3>> cur;
      for (const int t : tris) cur[t] = m.tris[t].v;
      const auto area = [&](const std::array<int, 3>& f) {
        return (at.at(f[1]) - at.at(f[0])).cross(at.at(f[2]) - at.at(f[0])).dot(nrm);
      };
      bool any = false;
      for (int pass = 0; pass < kInsetFlipPasses; ++pass) {
        std::map<EdgeKey, std::vector<int>> use;
        for (const auto& [t, f] : cur)
          for (int i = 0; i < 3; ++i)
            use[{std::min(f[i], f[(i + 1) % 3]), std::max(f[i], f[(i + 1) % 3])}].push_back(t);
        bool flipped = false;
        for (const auto& [e, ts] : use) {
          if (ts.size() != 2) continue;  // a boundary of the surface, not a seam in it
          const std::array<int, 3>&f0 = cur[ts[0]], &f1 = cur[ts[1]];
          int a = -1, b = -1, c = -1, d = -1;
          for (int i = 0; i < 3; ++i)
            if ((f0[i] == e.first && f0[(i + 1) % 3] == e.second) ||
                (f0[i] == e.second && f0[(i + 1) % 3] == e.first)) {
              a = f0[i];
              b = f0[(i + 1) % 3];
              c = f0[(i + 2) % 3];
            }
          for (const int x : f1)
            if (x != a && x != b) d = x;
          if (a < 0 || d < 0) continue;
          if (barred.count(a) || barred.count(b) || barred.count(c) || barred.count(d)) continue;
          if (use.count({std::min(c, d), std::max(c, d)})) continue;  // the flip already exists
          const int was = (area(f0) < -1e-12) + (area(f1) < -1e-12);
          if (!was) continue;
          const std::array<int, 3> g0{a, d, c}, g1{d, b, c};
          const double s0 = area(g0), s1 = area(g1);
          if ((s0 < -1e-12) + (s1 < -1e-12) >= was) continue;
          if (std::abs(s0) < 1e-18 || std::abs(s1) < 1e-18) continue;
          cur[ts[0]] = g0;
          cur[ts[1]] = g1;
          flipped = any = true;
          break;
        }
        if (!flipped) break;
      }
      if (!any) continue;
      for (const int t : tris)
        if (cur[t] != m.tris[t].v)
          surfaceOverride[t] = {at.at(cur[t][0]), at.at(cur[t][1]), at.at(cur[t][2])};
     }
  }

  // Trim the folds out of the inset map before anything is emitted.
  //
  // A surface's boundary chain is set back edge by edge, and the chain is only
  // faithful while every link still runs the way its source edge ran. Where two
  // boundary creases meet shallowly the corner mitre travels further ALONG the
  // boundary than the next boundary vertex does; the two cross, the link between
  // them runs backwards, and the offset boundary has a spike — a self-intersection.
  // The sliver of face between the crossing pair is then emitted inside-out, lying
  // in the face plane on top of its neighbours as an exact 180 deg fold pair, which
  // stays edge-manifold and so passes every gate while shading as a bright dart.
  //
  // An offset is trimmed by dropping what the crossing swallowed, and the mesh
  // counterpart of that is an edge collapse: seat the swallowed vertex's inset on
  // the surviving one. The reversed link becomes a point, the slivers hanging off it
  // become degenerate (OutMesh::tri drops them) and everything else re-attaches to
  // the survivor. Of the crossing pair the survivor is the one that advanced further
  // along the link — the mitre that did the swallowing is the one that actually bounds
  // the offset region. Every incident triangle seated at the same point moves with it, so
  // the collapse follows the whole sector, which is what keeps the strips, seams and
  // corner patches welded to the face; and one crossing can expose the next, so the
  // sweep repeats until the chain runs forward everywhere.
  void repairInsetFolds()
  {
    std::vector<std::vector<int>> vTris(m.pos.size());
    for (size_t t = 0; t < m.tris.size(); ++t)
      for (const int u : m.tris[t].v) vTris[u].push_back(static_cast<int>(t));

    for (int sweep = 0; sweep < kFoldRepairSweeps; ++sweep) {
      bool collapsed = false;
      for (const auto& [e, ts] : adj) {
        // Only a surface boundary is a chain of the offset; an interior seam moves
        // with the surface and cannot spike. Same test sectorOf bounds a sector by.
        if (ts.size() != 2 || surfaceOf[ts[0]] == surfaceOf[ts[1]]) continue;
        const double L = (m.pos[e.second] - m.pos[e.first]).norm();
        if (L < 1e-12) continue;
        const Vector3d eh = (m.pos[e.second] - m.pos[e.first]) / L;
        for (const int t : ts) {
          const Vector3d Pa = insetForTri(e.first, t), Pb = insetForTri(e.second, t);
          // How far each end has advanced along the link toward the other. Their sum
          // exceeding the link's length is exactly the reversal.
          const double sa = (Pa - m.pos[e.first]).dot(eh), sb = (m.pos[e.second] - Pb).dot(eh);
          if (sa + sb <= L) continue;
          // Two different things reverse a link, and only one of them is a spike. When
          // ONE end has run past the far end on its own, that end's mitre has swallowed
          // the link and the trim is to seat the far end on it. When neither has — the
          // two ends simply eat the link from opposite sides — the feature is narrower
          // than the blend, which is the size regime the strips and corner patches
          // already answer for; there is no spike to trim and moving a seat there only
          // unseats them.
          if (sa <= L && sb <= L) continue;
          const bool keepA = sa > sb;
          const int lose = keepA ? e.second : e.first;
          const Vector3d keep = keepA ? Pa : Pb;
          const Vector3d at = keepA ? Pb : Pa;
          if ((keep - at).squaredNorm() < 1e-24) continue;
          // A mitre is where two offset lines cross, and when they run near-parallel
          // that crossing races off down the boundary — tens of millimetres from its
          // own vertex on a fraction-of-a-millimetre link. Such a point is an artefact
          // of the intersection, not a seat: collapsing onto it would teleport a strip
          // across the solid. Nothing further than a couple of setbacks from the vertex
          // it belongs to is a survivor.
          const int win = keepA ? e.first : e.second;
          if ((keep - m.pos[win]).norm() >
              kFoldSurvivorSetbacks * (selected.count(e) ? setback(e) : size))
            continue;
          // Only a plain link in a chain may be trimmed. A vertex where more than two
          // surface boundaries meet is a junction — a crowded corner, a tangent
          // meeting of two walls — and its seat is what the corner patches and the
          // shared cross-sections are all built against. Merging it away pinches
          // them together instead of trimming an offset, and the pinch is two shells
          // touching at a point: closed, edge-manifold, and invalid.
          std::set<int> chain;
          for (const int tt : vTris[lose])
            for (int i = 0; i < 3; ++i) {
              const int a = m.tris[tt].v[i], b = m.tris[tt].v[(i + 1) % 3];
              if (a != lose && b != lose) continue;
              const auto jt = adj.find(EdgeKey{std::min(a, b), std::max(a, b)});
              if (jt != adj.end() && jt->second.size() == 2 &&
                  surfaceOf[jt->second[0]] != surfaceOf[jt->second[1]])
                chain.insert(a == lose ? b : a);
            }
          if (chain.size() != 2) continue;
          for (const int tt : vTris[lose])
            if ((insetForTri(lose, tt) - at).squaredNorm() < 1e-18)
              insetCollapse[{lose, tt}] = keep;
          collapsed = true;
        }
      }
      if (!collapsed) return;
    }
  }

  // A mixed corner's tangent control point at a boundary vertex: pushed inward off
  // the boundary along the fillet tangent (the chord to the shared centre Q,
  // projected off the boundary normal), so a curve leaving here starts tangent to
  // the incident fillet (G1) and bulges rather than diving straight across. Shared
  // by both saddle tessellations. arm is the control-arm length as a fraction of the
  // chord.
  static Vector3d tangentCtrl(const Vector3d& P, const Vector3d& N, const Vector3d& Q,
                              double arm)
  {
    const Vector3d d = Q - P;
    Vector3d t = d;
    const double nl = N.norm();
    if (nl > 1e-9) {
      const Vector3d Nu = N / nl;
      t = d - d.dot(Nu) * Nu;
    }
    const double tl = t.norm();
    t = tl > 1e-9 ? Vector3d(t / tl) : d.normalized();
    return P + arm * d.norm() * t;
  }

  // The three rolls that actually meet over a mixed corner: the concave fillet's own
  // cylinder, the two convex round-over cylinders, and — between them — the closing
  // transition, the ball still rolling but now riding on the two ALREADY-ROUNDED
  // convex edges at once instead of on their flat faces.
  struct CornerRoll {
    std::vector<Vector3d> C;           // the transition's ball centres, concave end first
    Vector3d ccP, ccD;                 // the concave fillet's axis: a point and a direction
    std::array<Vector3d, 2> axP, axD;  // the two round-overs' axes, likewise
    bool ok() const { return C.size() >= 3; }
  };

  // Solve that transition for one mixed ring.
  //
  // The three static surfaces at a mixed vertex genuinely share no point — the concave
  // fillet cylinder ends short of both round-overs — so the loft has to fill the space
  // between them, and lofting a short boundary onto a long one fans a web across it.
  // But the space is not surfaceless. As the ball leaves the trough it keeps two
  // contacts, one on each round-over, and its envelope is a canal surface: a
  // one-parameter family of shrinking circular arcs that continues the concave
  // fillet's end cross-section and tapers to a single point on the face the two
  // round-overs share. That surface, not a fan, is what belongs over the corner.
  //
  // A ball riding on a round-over of radius `size` is externally tangent to it, so its
  // centre is 2*size off that round-over's axis and the centre curve is the
  // intersection of the two tubes of radius 2*size about the two axes. March it from
  // the concave arc's own centre — there the ball still fills the trough and the
  // envelope is exactly the concave end section, so the handoff to that strip is free
  // — until the two contacts meet at the pinch.
  //
  // Every input is read back off the ring: each arc's rolling-ball centre is
  // C = P - size * ringNrm (the reconstruction the flush clamp already uses) and each
  // arc's axis direction is the normal of its own arc's plane. Nothing is fitted and
  // no surface is assumed. A result that is not ok() means the corner is not this
  // canonical shape — two round-overs closing one crease, well conditioned — and the
  // caller keeps its plain field untouched.
  CornerRoll cornerRoll(const std::vector<int>& ring, const std::vector<int>& ringSign,
                        const std::vector<int>& cc, const std::vector<int>& cv,
                        const std::vector<Vector3d>& cvC, const Vector3d& C0,
                        const Vector3d& Q) const
  {
    // The axis direction of one ring arc: the normal of the plane its points lie in,
    // which for a cross-section of a roll is the edge the roll swept along.
    auto arcAxis = [&](const std::vector<int>& idx, int lo, int hi, Vector3d& d) {
      Vector3d nrm = Vector3d::Zero();
      const Vector3d P0 = out.V[ring[idx[lo]]];
      for (int k = lo; k + 1 < hi; ++k)
        nrm += (out.V[ring[idx[k]]] - P0).cross(out.V[ring[idx[k + 1]]] - P0);
      const double nl = nrm.norm();
      if (nl < 1e-12) return false;
      d = nrm / nl;
      return true;
    };

    // The concave arc's own axis, where the ring carries enough of it to read one. Too
    // coarse an arc simply leaves it zero, and the sample that would have gone to the
    // concave cylinder stays on the leading ball instead — the two osculate there.
    CornerRoll R;
    R.ccP = C0;
    R.ccD = Vector3d::Zero();
    if (static_cast<int>(cc.size()) >= 3)
      arcAxis(cc, 0, static_cast<int>(cc.size()), R.ccD);

    const int Bn = static_cast<int>(cv.size());
    std::vector<std::pair<int, int>> runs;  // convex runs [lo,hi) inside cv
    for (int k = 0; k < Bn; ++k) {
      if (ringSign[cv[k]] <= 0) continue;
      if (k == 0 || ringSign[cv[k - 1]] <= 0) runs.emplace_back(k, k + 1);
      else runs.back().second = k + 1;
    }
    if (runs.size() != 2) return {};

    // Each round-over as a line: a point (its cross-section's rolling-ball centre)
    // and a direction (the normal of the cross-section's plane, which is the edge).
    for (int r = 0; r < 2; ++r) {
      const int lo = runs[r].first, hi = runs[r].second;
      if (hi - lo < 3 || !arcAxis(cv, lo, hi, R.axD[r])) return {};
      Vector3d cen = Vector3d::Zero();
      for (int k = lo; k < hi; ++k) cen += cvC[k];
      cen /= static_cast<double>(hi - lo);
      // The arc must really be one cross-section of one roll, or the reconstruction
      // is meaningless (a clamped or twisted arc scatters its centres).
      double spread = 0;
      for (int k = lo; k < hi; ++k) spread = std::max(spread, (cvC[k] - cen).norm());
      if (spread > kRollArcScatterFrac * size) return {};
      R.axP[r] = cen;
    }
    const std::array<Vector3d, 2>& axP = R.axP;
    const std::array<Vector3d, 2>& axD = R.axD;

    // Distance from a centre to one round-over's axis, and the unit direction that
    // increases it — the ball touches that round-over at c - size * g.
    auto tube = [&](const Vector3d& c, int r, Vector3d& g) {
      const Vector3d w = c - axP[r];
      const Vector3d v = w - w.dot(axD[r]) * axD[r];
      const double d = v.norm();
      if (d < 1e-12) { g = Vector3d::Zero(); return 0.0; }
      g = v / d;
      return d;
    };

    // The concave arc's own centre must already be a ball of the family, or this is
    // not the canonical two-round-overs-closing-one-crease corner.
    Vector3d g0a, g0b;
    if (std::abs(tube(C0, 0, g0a) - 2 * size) > kRollTubeTolFrac * size) return {};
    if (std::abs(tube(C0, 1, g0b) - 2 * size) > kRollTubeTolFrac * size) return {};

    std::vector<Vector3d> chain{C0};
    Vector3d c = C0, prevT = Vector3d::Zero();
    double prevAng = std::numeric_limits<double>::max();
    const double h = kRollMarchStepFrac * size;
    for (int step = 0; step < kRollMarchMaxSteps; ++step) {
      Vector3d ga, gb;
      tube(c, 0, ga);
      tube(c, 1, gb);
      Vector3d t = ga.cross(gb);
      const double tl = t.norm();
      if (tl < 1e-6) break;  // the two contacts have merged, or the axes graze
      t /= tl;
      // March away from the strip, into the corner: toward the ring's own centroid
      // on the first step, then by continuity. Positional, so the marched chain does
      // not depend on which incident triangle the fan happened to start on.
      const double orient = prevT.squaredNorm() > 0 ? t.dot(prevT) : t.dot(Q - C0);
      if (orient == 0.0) return {};
      if (orient < 0) t = -t;
      prevT = t;
      Vector3d cn = c + h * t;
      for (int it = 0; it < kRollNewtonIters; ++it) {  // Newton back onto both tubes
        Vector3d na, nb;
        const double fa = tube(cn, 0, na) - 2 * size, fb = tube(cn, 1, nb) - 2 * size;
        if (std::abs(fa) < 1e-12 * size && std::abs(fb) < 1e-12 * size) break;
        const double g = na.dot(nb), det = 1 - g * g;
        if (std::abs(det) < 1e-9) return {};
        cn += ((-fa + g * fb) / det) * na + ((-fb + g * fa) / det) * nb;
      }
      Vector3d ea, eb;
      const double da = tube(cn, 0, ea), db = tube(cn, 1, eb);
      if (std::abs(da - 2 * size) > 1e-6 * size || std::abs(db - 2 * size) > 1e-6 * size)
        break;  // the correction did not land on the curve; stop where it still did
      // The characteristic arc spans the two contact directions -ea, -eb; the march
      // ends when they meet (the pinch) and must never run past it.
      const double ang = std::acos(std::clamp(ea.dot(eb), -1.0, 1.0));
      if (ang > prevAng) break;
      chain.push_back(cn);
      prevAng = ang;
      c = cn;
      if (ang < 1e-3) break;
    }
    if (chain.size() < 3) return {};
    R.C.swap(chain);
    return R;
  }

  // Where a sample belongs on the corner's real surface. Over the corner the surface
  // is three rolls stitched along tangent seams: the concave fillet cylinder where the
  // ball still fills the trough, then the transition's canal surface, then — past each
  // of the transition's two contacts — that round-over's own cylinder. So classify by
  // the nearest transition ball and project onto the roll that owns the sample: the
  // sphere of that ball inside the characteristic arc, the concave cylinder before the
  // march starts, a round-over cylinder outside the arc on that contact's side. The
  // seams are the contact curves, where the canal is tangent to the cylinder it hands
  // off to, so a sample crossing one moves continuously and the patch shows no crease.
  Vector3d rollProject(const CornerRoll& R, const Vector3d& p) const
  {
    auto onCyl = [&](const Vector3d& P, const Vector3d& D) {
      const Vector3d f = P + (p - P).dot(D) * D;
      const Vector3d v = p - f;
      const double d = v.norm();
      return d < 1e-9 ? p : Vector3d(f + (size / d) * v);
    };

    // Nearest ball centre on the marched chain.
    double best = std::numeric_limits<double>::max();
    Vector3d c = R.C.front();
    double bestT = 0;
    size_t bestK = 0;
    for (size_t k = 0; k + 1 < R.C.size(); ++k) {
      const Vector3d a = R.C[k], ab = R.C[k + 1] - a;
      const double L2 = ab.squaredNorm();
      const double t = L2 > 1e-18 ? std::clamp((p - a).dot(ab) / L2, 0.0, 1.0) : 0.0;
      const Vector3d q = a + t * ab;
      const double d = (p - q).norm();
      if (d < best) { best = d; c = q; bestK = k; bestT = t; }
    }

    // The ball at c touches round-over r at c - size*g[r], so the characteristic arc
    // runs between those two directions in the plane the march is normal to.
    std::array<Vector3d, 2> g;
    for (int r = 0; r < 2; ++r) {
      const Vector3d w = c - R.axP[r];
      const Vector3d v = w - w.dot(R.axD[r]) * R.axD[r];
      const double d = v.norm();
      if (d < 1e-12) return p;
      g[r] = v / d;
    }
    Vector3d t = g[0].cross(g[1]);
    const double tl = t.norm();
    if (tl < 1e-9) return p;  // at the pinch the arc has closed; leave the sample
    t /= tl;
    if (t.dot(R.C.back() - R.C.front()) < 0) t = -t;

    // Before the march even starts the ball is still the trough's own, so the sample
    // belongs to the concave fillet cylinder rather than to the leading sphere.
    if (bestK == 0 && bestT <= 0.0 && (p - c).dot(t) < 0 && R.ccD.squaredNorm() > 0.5)
      return onCyl(R.ccP, R.ccD);

    const Vector3d a = -g[0], b = -g[1];
    Vector3d u = p - c;
    u -= u.dot(t) * t;
    const double ul = u.norm();
    if (ul < 1e-12) return p;
    u /= ul;
    const double ref = a.cross(b).dot(t);
    if (std::abs(ref) > 1e-12 && a.cross(u).dot(t) * ref > 0 && u.cross(b).dot(t) * ref > 0) {
      const Vector3d rv = p - c;               // inside the arc: the transition's own ball
      const double d = rv.norm();
      return d < 1e-9 ? p : Vector3d(c + (size / d) * rv);
    }
    const int r = u.dot(a) >= u.dot(b) ? 0 : 1;  // past a contact: that round-over
    return onCyl(R.axP[r], R.axD[r]);
  }

  // The rolling ball's own geometry at such a corner, read off the concave section.
  //
  // C0 is the concave section's centre, e the crease direction pointing into the solid
  // and m the shared face's outward normal. For swing direction u the ball's centre sits
  // one diameter out along u, slid along e by just enough to keep it one radius clear of
  // the face; the surface is one radius off that centre, on the arc running from the
  // concave contact (-u) round to the face contact (m). Where the face is square to the
  // crease that slide is nil and that arc is a quarter turn for every u — the swing is a
  // circle and the sweep a torus. Rake the face and the swing stretches into an ellipse
  // and the arc's turn opens and closes along it, but it is the same ball on the same
  // two contacts throughout.
  struct CornerCanal
  {
    Vector3d C0 = Vector3d::Zero(), e = Vector3d::Zero(), m = Vector3d::Zero(),
             uTop = Vector3d::Zero();
    double r = 0;
    // How far along the crease the centre slides. Zero at uTop, the swing direction that
    // leans furthest into the face — so the concave section seats there and every other
    // direction slides back off it, never past it.
    double lift(const Vector3d& u) const { return -2 * r * (u - uTop).dot(m) / e.dot(m); }
    Vector3d centre(const Vector3d& u) const { return C0 + 2 * r * u + lift(u) * e; }
    Vector3d at(const Vector3d& u, double t) const
    {
      const Vector3d n1 = -u;
      Vector3d w = m - m.dot(n1) * n1;
      const double wl = w.norm();
      if (wl < 1e-12) return centre(u) + r * n1;
      const double a = t * std::acos(std::clamp(n1.dot(m), -1.0, 1.0));
      return centre(u) + r * (std::cos(a) * n1 + (std::sin(a) / wl) * w);
    }
  };

  // The corner where two convex round-overs close over a concave crease, built as the
  // one piece of surface it actually is.
  //
  // Keep rolling the ball into the corner. Its centre leaves the concave section and
  // swings round until it reaches each round-over's axis, staying one diameter off the
  // crease axis and one radius clear of the shared face the whole way; the ball sweeping
  // that swing traces a tube, and the piece of it between the concave section and the
  // face IS the corner. Its first and last cross-sections are the two round-over strips'
  // end sections exactly, and it runs into the shared face tangentially along an arc, so
  // the two round-overs meet each other smoothly instead of at a mitre. Nothing is
  // lofted and nothing is fitted — every sample is one radius off a centre the ring
  // itself hands over.
  //
  // On a raked face the swing also slides along the crease, so the tube's concave-side
  // boundary is a slanted cut of the concave fillet cylinder rather than the strip's own
  // square end section. The strip is seated at the deepest point of that cut, and the
  // gap between the two is filled by the run of the cylinder itself — an extra column of
  // the same grid, degenerate where the face is square.
  //
  // The shared face is left flat inside the tangent arc: the tube only touches it, so
  // the sliver between the arc and the ring's own mitre is a plain planar patch,
  // coplanar with the face it continues.
  //
  // The ring must be exactly this corner and is checked to be: three arcs of arcSegs+1
  // samples each, the concave one a circle of the blend radius pinning the swing, and
  // the tube built off that swing landing back on the ring's two round-over arcs sample
  // for sample. That last check is the whole gate — it passes only if the strips and the
  // corner describe one surface. Anything else returns false and the caller keeps its
  // loft.
  bool emitCornerTube(const std::vector<int>& ring, const std::vector<Vector3d>& ringNrm,
                      const std::vector<int>& ringSign)
  {
    const int k = arcSegs;
    const int n = static_cast<int>(ring.size());
    if (k < 2 || n < 3 * k + 2 || n > 3 * k + 6) return false;
    if (static_cast<int>(ringNrm.size()) != n || static_cast<int>(ringSign.size()) != n)
      return false;
    const double tol = kTubeFitTolFrac * size;

    // The shared face's normal is a connector's own face normal — but which connector is
    // the shared face's is not known ahead of the fit, so every distinct one is tried.
    std::vector<Vector3d> mcand;
    for (int i = 0; i < n; ++i) {
      if (ringSign[i] != 0) continue;
      bool dup = false;
      for (const Vector3d& q : mcand) dup = dup || (q - ringNrm[i]).norm() < 1e-9;
      if (!dup) mcand.push_back(ringNrm[i]);
    }

    // The ring is one round-over arc, the concave arc and the other round-over arc, in
    // that cyclic order, each arcSegs+1 samples, separated by a shared end sample (gap
    // 0), nothing (gap 1) or one connector (gap 2); whatever is left over after the
    // three is the shared face's mitre. Which sample carries which sign depends on the
    // fan walk (an arc's shared end keeps the sign of whichever arc reached it first),
    // so the split is not read off the signs: every rotation, direction and pair of gaps
    // is tried and the geometry decides. It is exact, so at most one can pass.
    CornerCanal cc;
    cc.r = size;
    std::vector<Vector3d> uc(k + 1);  // the swing directions, off the concave arc itself
    Vector3d np = Vector3d::Zero();   // the concave arc's plane normal: the crease's line
    int lead = 0, dir = 1, gA = 0, gB = 0;
    auto at = [&](int p) { return ((lead + dir * p) % n + n) % n; };
    auto P = [&](int p) { return out.V[ring[at(p)]]; };

    // The concave arc must be one circle of radius size: its centre is the swing's own
    // and its plane is square to the crease. Three samples pin both.
    auto fitArc = [&]() {
      const int c0 = k + gA;
      const Vector3d p0 = P(c0), p1 = P(c0 + (k + 1) / 2), p2 = P(c0 + k);
      const Vector3d e0 = p1 - p0, e1 = p2 - p0;
      np = e0.cross(e1);
      const double nl = np.norm();
      if (nl < 1e-12) return false;
      np /= nl;
      const Vector3d mid0 = 0.5 * (p0 + p1), mid1 = 0.5 * (p0 + p2);
      cc.C0 = lineIntersect(mid0, np.cross(e0).normalized(), mid1, np.cross(e1).normalized());
      for (int i = 0; i <= k; ++i) {
        const Vector3d w = P(c0 + i) - cc.C0;
        if (std::abs(w.norm() - size) > tol || std::abs(w.dot(np)) > tol) return false;
        uc[i] = w.normalized();
      }
      return std::abs(uc[0].dot(uc[k])) < kMinSwingTurnCos;  // the swing must really turn
    };

    auto fits = [&](const Vector3d& mc) {
      cc.m = mc;
      // Of the crease's two directions the solid's is the one leading away from the
      // face; a crease running along the face has no corner of this shape at all.
      cc.e = np.dot(mc) < 0 ? np : Vector3d(-np);
      if (cc.e.dot(mc) > -1e-6) return false;
      // The concave section seats at the swing direction leaning furthest into the face,
      // which is one of the arc's two ends — and the lift test below is what says so: it
      // fails if any interior direction leans further. (Reading the maximum off the
      // samples instead would read floating-point noise on a face square to the crease,
      // where every direction leans the same nil amount.)
      cc.uTop = uc[0].dot(mc) >= uc[k].dot(mc) ? uc[0] : uc[k];
      for (int i = 0; i <= k; ++i)
        if (cc.lift(uc[i]) > tol) return false;
      // The tube's two end cross-sections must land back on the ring's two round-over
      // arcs, sample for sample, or the strips and the corner are not one surface.
      const int b0 = 2 * k + gA + gB;
      for (int j = 0; j <= k; ++j) {
        const double t = static_cast<double>(j) / k;
        if ((cc.at(uc[0], t) - P(k - j)).norm() > tol) return false;
        if ((cc.at(uc[k], t) - P(b0 + j)).norm() > tol) return false;
      }
      return true;
    };

    bool found = false;
    for (int d = 0; d < 2 && !found; ++d)
      for (int l = 0; l < n && !found; ++l)
        for (int a = 0; a < 3 && !found; ++a)
          for (int b = 0; b < 3 && !found; ++b) {
            if (3 * k + a + b + 1 > n) continue;
            dir = d ? -1 : 1;
            lead = l;
            gA = a;
            gB = b;
            if (!fitArc()) continue;
            // Square to the crease, the shared face's normal is the concave arc's own
            // plane normal and no connector is needed to name it; raked, it is not, and
            // only a connector on that face carries it. Both are offered and the arcs
            // decide, so neither case depends on which face the ring's mitre landed on.
            for (const Vector3d& mc : mcand)
              if (fits(mc)) { found = true; break; }
            if (!found && (fits(np) || fits(Vector3d(-np)))) found = true;
          }
    if (!found) return false;
    const int c0 = k + gA, b0 = 2 * k + gA + gB;

    // Column 0 is the concave strip's end section, column 1 the slanted cut the tube
    // really starts on, columns 1..k+1 the swing's arc. Rows 0 and k are the two
    // round-over sections. Where the face is square the first two columns coincide and
    // the grid is the plain tube.
    std::vector<std::vector<int>> g(k + 1, std::vector<int>(k + 2));
    for (int i = 0; i <= k; ++i) {
      g[i][0] = ring[at(c0 + i)];
      for (int j = 1; j <= k + 1; ++j) {
        if (i == 0) { g[i][j] = ring[at(k - j + 1)]; continue; }
        if (i == k) { g[i][j] = ring[at(b0 + j - 1)]; continue; }
        const Vector3d p = cc.at(uc[i], static_cast<double>(j - 1) / k);
        g[i][j] = (p - out.V[g[i][0]]).norm() < 1e-9 ? g[i][0] : out.add(p);
      }
    }

    std::vector<std::array<int, 3>> tris;
    auto push = [&](int a, int b, int c) {
      if (a != b && b != c && a != c) tris.push_back({a, b, c});
    };
    for (int i = 0; i < k; ++i)
      for (int j = 0; j <= k; ++j) {
        push(g[i][j], g[i + 1][j], g[i + 1][j + 1]);
        push(g[i][j], g[i + 1][j + 1], g[i][j + 1]);
      }
    // The flat sliver: the tube's tangent arc on the shared face, closed back over the
    // ring's mitre. It reuses the arc's own vertices, so no T-junction.
    std::vector<int> sliver;
    for (int i = 0; i <= k; ++i)
      if (sliver.empty() || sliver.back() != g[i][k + 1]) sliver.push_back(g[i][k + 1]);
    for (int p = b0 + k + 1; p < n; ++p)
      if (sliver.back() != ring[at(p)]) sliver.push_back(ring[at(p)]);
    while (sliver.size() > 1 && sliver.front() == sliver.back()) sliver.pop_back();
    if (sliver.size() >= 3) {
      std::vector<std::array<int, 3>> flat;
      if (!earClipRing(sliver, flat)) return false;
      for (const auto& t : flat) push(sliver[t[0]], sliver[t[1]], sliver[t[2]]);
    }

    std::map<std::pair<int, int>, int> edgeUse;
    for (const auto& t : tris)
      for (const auto& e : {std::minmax(t[0], t[1]), std::minmax(t[1], t[2]),
                            std::minmax(t[0], t[2])})
        if (++edgeUse[e] > 2) return false;
    for (const auto& t : tris) out.tri(t[0], t[1], t[2]);
    return true;
  }

  // Solution A: a discrete transfinite (Coons-family) saddle across a mixed corner
  // ring, replacing the pole-free concentric fill where the ring cleanly partitions.
  //
  // A mixed ring is one contiguous concave arc (the crease dying into the face) and,
  // opposite it, the complementary run carrying the two convex round-overs and any
  // sector-inset connectors between them. The two are joined at their ends by a
  // single ring edge each (the ring is a cycle, so removing one contiguous concave
  // run leaves one contiguous complement — the two share exactly two boundary edges).
  //
  // The patch is a transfinite blend of the two opposite boundaries: for a pair of
  // points (one on the concave arc, one on the convex run) the cross curve between
  // them is a cubic Bezier B(s) with the two fillet-tangent controls, so it leaves
  // the concave boundary along the concave fillet tangent and the convex boundary
  // along the convex fillet tangent (G1 to both strips) and carries their opposite
  // curvatures independently — a genuine saddle with NO interior pole (every interior
  // vertex is distinct; nothing collapses to a centre). The two boundaries have
  // different vertex counts (a convex run is ~2x a concave arc), so they are lofted
  // by an arc-length merge: each merge cross-link becomes a K-sample cross Bezier and
  // adjacent cross curves are stitched by a second merge, tapering cleanly where the
  // longer side advances alone. The two end links are the ring's connector edges and
  // stay straight (2 samples) so no vertex is inserted on a shared boundary edge.
  //
  // Returns false (caller falls back to the concentric emitSaddle) when the ring does
  // not fit this partition — not exactly one concave run, too few convex/concave
  // points — or when the resulting patch would be edge-non-manifold.
  bool emitCoonsSaddle(const std::vector<int>& ring, const std::vector<Vector3d>& ringNrm,
                       const std::vector<int>& ringSign)
  {
    const int n = static_cast<int>(ring.size());
    if (n < 6 || static_cast<int>(ringNrm.size()) != n ||
        static_cast<int>(ringSign.size()) != n)
      return false;

    // Exactly one contiguous cyclic concave run, with enough convex mass opposite it.
    int nConcave = 0, nConvex = 0, runs = 0, runStart = -1;
    for (int i = 0; i < n; ++i) {
      if (ringSign[i] < 0) ++nConcave;
      else if (ringSign[i] > 0) ++nConvex;
      if (ringSign[i] < 0 && ringSign[(i - 1 + n) % n] >= 0) { ++runs; runStart = i; }
    }
    if (runs != 1 || nConcave < 2 || nConvex < 2) return false;

    // Concave arc Cc = the run; convex run Cv = the complement, taken anti-parallel so
    // Cv[0] is adjacent to Cc[0] and Cv[last] to Cc[last] (the two connector edges).
    std::vector<int> cc, cv;  // ring indices
    for (int i = runStart; ringSign[i] < 0; i = (i + 1) % n) {
      cc.push_back(i);
      if (static_cast<int>(cc.size()) > n) return false;
    }
    const int A = static_cast<int>(cc.size());
    const int cvStop = (runStart + A) % n;  // first vertex of the complement (Cv last)
    for (int i = (runStart - 1 + n) % n;; i = (i - 1 + n) % n) {
      cv.push_back(i);
      if (i == cvStop) break;
      if (static_cast<int>(cv.size()) > n) return false;
    }
    const int Bn = static_cast<int>(cv.size());
    if (A < 2 || Bn < 2) return false;

    Vector3d Q = Vector3d::Zero();
    for (int i = 0; i < n; ++i) Q += out.V[ring[i]];
    Q /= static_cast<double>(n);

    // Boundary positions, tangent controls, normalised arc-length parameters, and the
    // rolling-ball centre of the fillet surface at each boundary point. The ring normal
    // is the outward radius of that surface, ringNrm[i] = unit(P - C), and every arc
    // boundary point lies one radius off its centre, so C = P - size * ringNrm[i]
    // reconstructs the centre the near-seam clamp (below) projects onto.
    auto build = [&](const std::vector<int>& idx, std::vector<Vector3d>& P,
                     std::vector<Vector3d>& M, std::vector<double>& t, std::vector<Vector3d>& C) {
      const int m = static_cast<int>(idx.size());
      P.resize(m); M.resize(m); t.resize(m); C.resize(m);
      for (int i = 0; i < m; ++i) P[i] = out.V[ring[idx[i]]];
      double acc = 0; t[0] = 0;
      for (int i = 1; i < m; ++i) { acc += (P[i] - P[i - 1]).norm(); t[i] = acc; }
      for (int i = 1; i < m; ++i) t[i] = acc > 1e-12 ? t[i] / acc : static_cast<double>(i) / (m - 1);
      for (int i = 0; i < m; ++i) {
        M[i] = tangentCtrl(P[i], ringNrm[idx[i]], Q, kCoonsArmFrac);
        C[i] = P[i] - size * ringNrm[idx[i]].normalized();
      }
    };
    std::vector<Vector3d> ccP, ccM, cvP, cvM, ccC, cvC;
    std::vector<double> ccT, cvT;
    build(cc, ccP, ccM, ccT, ccC);
    build(cv, cvP, cvM, cvT, cvC);

    // The corner's real rolls, if this corner is the canonical one. The concave arc is
    // one cross-section of one roll, so its ring points share a single reconstructed
    // centre — the ball that still fills the trough, and the march's start. A scattered
    // reconstruction means the arc is not that, and the patch stays on the plain field.
    CornerRoll roll;
    {
      Vector3d C0 = Vector3d::Zero();
      for (const auto& p : ccC) C0 += p;
      C0 /= static_cast<double>(ccC.size());
      bool tight = true;
      for (const auto& p : ccC) tight = tight && (p - C0).norm() <= kRollArcScatterFrac * size;
      if (tight) roll = cornerRoll(ring, ringSign, cc, cv, cvC, C0, Q);
    }

    // Cross-curve samples. Normally the strip's own arcSegs, so the patch is sampled
    // exactly as densely as the strips it continues. Where the roll is known there is a
    // floor under it: at stock settings a small radius earns only a handful of
    // fragments, which leaves the patch with almost no interior at all — one layer per
    // row — and a field with nothing to move cannot shape anything, so the corner keeps
    // the coarse pinwheel the raw loft folds into. Only the INTERIOR densifies. Layer 0
    // and layer K are the two ring points the row hangs between, so the ring keeps its
    // exact vertex count and positions and the extra layers land strictly between them:
    // no new boundary vertex, no T-junction against a strip. The floor is inactive as
    // soon as the tessellation earns more than it, so nothing changes at higher $fn.
    const int K = roll.ok() ? std::max(kCornerInteriorFloor, arcSegs) : std::max(2, arcSegs);

    // Push a proud sample back onto the rolling-ball fillet surface it stands over.
    // The surface at centre C is the sphere of radius size; convex fillet material is
    // INSIDE it (a sample farther than size stands proud), a concave valley is OUTSIDE
    // it (a sample nearer than size stands proud). Only proud samples move, and only by
    // the blend weight w, so the result is flush-or-slightly-inside, never a new dip.
    auto flush = [&](const Vector3d& p, const Vector3d& C, bool concave, double w) {
      const Vector3d rv = p - C;
      const double d = rv.norm();
      if (d < 1e-9 || w <= 0.0) return p;
      const bool proud = concave ? (d < size) : (d > size);
      if (!proud) return p;
      const Vector3d onSurf = C + size * (rv / d);
      return Vector3d(p + w * (onSurf - p));
    };

    // A cross curve from concave point i to convex point j, sampled at K+1 layers
    // (s=0 on the concave boundary, s=1 on the convex boundary); a connector edge is
    // the same curve at just its two endpoints. Returns the layer positions, which the
    // caller places once the whole field is laid out. The interior layers within the
    // outer band of each end (kCoonsFlushBand) are clamped flush onto that end's fillet
    // surface, the weight smoothstepped to zero across the band, so the patch continues
    // the strip near the seam instead of bulging proud; the boundary layers (s=0, s=1)
    // are left untouched so they stay welded to the ring.
    auto crossCurve = [&](int i, int j, bool edge, std::vector<Vector3d>& lay) {
      lay.clear();
      const int steps = edge ? 1 : K;
      for (int l = 0; l <= steps; ++l) {
        const double s = static_cast<double>(l) / steps;
        const double u = 1 - s;
        Vector3d p = u * u * u * ccP[i] + 3 * u * u * s * ccM[i] +
                     3 * u * s * s * cvM[j] + s * s * s * cvP[j];
        if (l > 0 && l < steps) {  // never move the welded boundary layers
          const double ec = std::clamp((kCoonsFlushBand - s) / kCoonsFlushBand, 0.0, 1.0);
          const double ev =
            std::clamp((kCoonsFlushBand - (1 - s)) / kCoonsFlushBand, 0.0, 1.0);
          p = flush(p, ccC[i], /*concave=*/true, ec * ec * (3 - 2 * ec));
          p = flush(p, cvC[j], /*concave=*/false, ev * ev * (3 - 2 * ev));
        }
        lay.push_back(p);
      }
    };

    // Collect triangles locally, then reject (fall back) if any edge is non-manifold.
    std::vector<std::array<int, 3>> tris;
    auto stitch = [&](const std::vector<int>& ga, const std::vector<double>& sa,
                      const std::vector<int>& gb, const std::vector<double>& sb) {
      int ia = 0, ib = 0;
      const int na = static_cast<int>(ga.size()) - 1, nb = static_cast<int>(gb.size()) - 1;
      while (ia < na || ib < nb) {
        bool advA;
        if (ia >= na) advA = false;
        else if (ib >= nb) advA = true;
        else advA = sa[ia + 1] <= sb[ib + 1];
        if (advA) { tris.push_back({ga[ia], ga[ia + 1], gb[ib]}); ++ia; }
        else { tris.push_back({ga[ia], gb[ib + 1], gb[ib]}); ++ib; }
      }
    };

    // The arc-length merge of the two boundaries, as the row sequence it walks.
    std::vector<std::pair<int, int>> seq{{0, 0}};
    for (int i = 0, j = 0; i < A - 1 || j < Bn - 1;) {
      bool advI;
      if (i >= A - 1) advI = false;
      else if (j >= Bn - 1) advI = true;
      else advI = ccT[i + 1] <= cvT[j + 1];
      i = advI ? i + 1 : i;
      j = advI ? j : j + 1;
      seq.emplace_back(i, j);
    }
    int R = static_cast<int>(seq.size());
    std::vector<std::vector<Vector3d>> grid(R);
    for (int r = 0; r < R; ++r)
      crossCurve(seq[r].first, seq[r].second, /*edge=*/r == 0 || r == R - 1, grid[r]);

    // Any row's layer l, reading a two-sample connector chord at the same s as the rest.
    auto rowAt = [&](const std::vector<Vector3d>& g, int l) {
      if (static_cast<int>(g.size()) == K + 1) return g[l];
      const double s = static_cast<double>(l) / K;
      return Vector3d(g.front() + s * (g.back() - g.front()));
    };

    // Refine the rows, the other half of the interior floor.
    //
    // The row count is set by the ring: one row per boundary point the merge steps
    // through. At stock settings that is a handful, so densifying only along the cross
    // curves leaves a grid long in one direction and starved in the other, and the
    // field drags those slivers into a blade instead of a surface. Insert M-1 rows
    // inside each merge step so the interior is refined both ways at once.
    //
    // An inserted row carries the SAME two ring endpoints as the step's first row, so
    // its layer 0 and layer K weld onto vertices the ring already has: no boundary
    // vertex is added, no ring edge is split, and the strip between two inserted rows
    // simply degenerates at both ends into the fan that closes back on those vertices
    // (out.tri drops the degenerate triangles the weld leaves). Each ring edge still
    // carries exactly the one triangle the unrefined merge gave it.
    if (roll.ok() && R >= 2) {
      const int M = std::max(1, (kCornerInteriorFloor + R - 2) / (R - 1));
      if (M > 1) {
        std::vector<std::vector<Vector3d>> fine;
        fine.reserve(static_cast<size_t>(R - 1) * M + 1);
        for (int r = 0; r + 1 < R; ++r) {
          fine.push_back(grid[r]);
          for (int m = 1; m < M; ++m) {
            const double f = static_cast<double>(m) / M;
            std::vector<Vector3d> row(K + 1);
            for (int l = 0; l <= K; ++l)
              row[l] = (1 - f) * rowAt(grid[r], l) + f * rowAt(grid[r + 1], l);
            row.front() = rowAt(grid[r], 0);  // stay on the ring vertices this step began on
            row.back() = rowAt(grid[r], K);
            fine.push_back(std::move(row));
          }
        }
        fine.push_back(grid[R - 1]);
        grid.swap(fine);
        R = static_cast<int>(grid.size());
      }
    }

    // Draw the interior of that field onto the corner's real rolls.
    //
    // Only interior samples move: the ring itself, both boundary layers of every row
    // and the ring's two connector edges (single straight chords carrying no interior
    // layer) stay exactly where the loft welded them. Those chords are also why the
    // pull is ramped in — over a margin of rows from each end, and across s from each
    // boundary layer — so a row never drops onto the roll while the neighbour it fans
    // onto cannot follow.
    //
    // A projection alone is not enough. It moves every sample by its own amount, and
    // the merge fans several rows off one shared boundary point, so the field arrives
    // both bunched (neighbouring layers a fortieth of a step apart) and pleated across
    // the fan. So alternate: relax the interior toward its four grid neighbours, then
    // draw it back onto the roll. The relaxation spreads the samples out and the
    // redraw undoes the caving it would otherwise leave, and after a few rounds the
    // patch is an evenly sampled piece of the real surface.
    if (roll.ok() && R > 4 && K >= 2) {
      std::vector<double> rw(R);
      for (int r = 0; r < R; ++r) {
        const double d = std::clamp(std::min(r, R - 1 - r) / kRollPullRowMargin, 0.0, 1.0);
        rw[r] = d * d * (3 - 2 * d);
      }
      std::vector<double> sw(K + 1);
      for (int l = 0; l <= K; ++l) {
        const double s = static_cast<double>(l) / K;
        const double d = std::clamp(std::min(s, 1 - s) / kRollPullSMargin, 0.0, 1.0);
        sw[l] = d * d * (3 - 2 * d);
      }
      auto at = [&](int r, int l) { return rowAt(grid[r], l); };
      // A sample is drawn onto the roll unless the pull is absurd — a rollProject that
      // has picked the wrong side of a degenerate configuration. How far is "absurd"
      // depends on where the sample sits: next to the ring a large pull would drag the
      // patch away from the weld it has to reach, while deep inside the patch a pull
      // approaching the patch's own depth is exactly what the corner asks for. So the
      // guard rides the same ramp the pull does, tight at the boundary and loose in the
      // middle, instead of one flat number that could clip a legitimate deep pull.
      auto draw = [&](int r, int l, Vector3d p) {
        const double w = rw[r] * sw[l];
        const Vector3d q = rollProject(roll, p);
        if ((q - p).norm() < (kRollPullGuardBase + kRollPullGuardSpan * w) * size)
          p += w * (q - p);
        return p;
      };
      for (int r = 1; r + 1 < R; ++r)
        if (static_cast<int>(grid[r].size()) == K + 1)
          for (int l = 1; l < K; ++l) grid[r][l] = draw(r, l, grid[r][l]);
      // enough rounds for the relaxation to reach across the fan
      for (int pass = 0; pass < kRollRelaxPasses; ++pass) {
        std::vector<std::vector<Vector3d>> next = grid;
        for (int r = 1; r + 1 < R; ++r) {
          if (static_cast<int>(grid[r].size()) != K + 1) continue;
          for (int l = 1; l < K; ++l) {
            const Vector3d avg =
                0.25 * (at(r - 1, l) + at(r + 1, l) + grid[r][l - 1] + grid[r][l + 1]);
            next[r][l] = draw(r, l, grid[r][l] + kRollRelaxRate * (avg - grid[r][l]));
          }
        }
        grid.swap(next);
      }
    }

    std::vector<int> curG, nxtG;
    std::vector<double> curS, nxtS;
    auto place = [&](int r, std::vector<int>& gid, std::vector<double>& sp) {
      gid.clear(); sp.clear();
      const int steps = static_cast<int>(grid[r].size()) - 1;
      for (int l = 0; l <= steps; ++l) {
        gid.push_back(out.add(grid[r][l]));
        sp.push_back(static_cast<double>(l) / steps);
      }
    };
    place(0, curG, curS);
    for (int r = 1; r < R; ++r) {
      place(r, nxtG, nxtS);
      stitch(curG, curS, nxtG, nxtS);
      curG.swap(nxtG); curS.swap(nxtS);
    }

    std::map<std::pair<int, int>, int> edgeUse;
    for (const auto& t : tris) {
      if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) continue;
      for (const auto& e : {std::minmax(t[0], t[1]), std::minmax(t[1], t[2]),
                            std::minmax(t[0], t[2])})
        if (++edgeUse[e] > 2) return false;  // non-manifold: fall back to concentric
    }
    for (const auto& t : tris) out.tri(t[0], t[1], t[2]);
    return true;
  }

  // Fill a mixed-sign vertex ring with a curved saddle rather than a flat
  // ear-clip. A convex edge and a concave edge sharing u cannot be capped by one
  // sphere (a ball is single-signed), so the patch is a genuine saddle: it must
  // curve outward where it continues the convex roundovers and inward where it
  // continues the concave valley.
  //
  // The patch is a tangent-continuous blend, NOT a faired membrane. A boundary-fixed
  // Laplacian (or any area-minimiser) relaxes to a minimal surface, which caves
  // toward the reentrant corner — it ignores where the incident fillets were
  // heading. Instead each boundary point i carries the fillet surface's own normal
  // there, ringNrm[i]; the patch leaves that point along the fillet's tangent (the
  // inward chord to the centre, projected off the normal), so it continues the roll
  // and holds the rolled radius out. Concretely each radial line from a boundary
  // point B[i] to the shared centre Q is a quadratic Bezier through a control point
  // M[i] = B[i] + |Q-B[i]| * tangent[i]: at the boundary its tangent is exactly the
  // fillet tangent (G1 to the strips), and it bulges outward before curving in to Q.
  // Opposite convex and concave sides carry opposite tangents, so the patch is a
  // genuine saddle. It is sampled on a concentric-ring topology (k layers boundary
  // -> centre, uniform quad bands) that tessellates evenly and scales with $fn;
  // being an analytic evaluation, not a solve, it is resolution-independent and
  // does not cave. Falls back (returns false) only on a degenerate ring.
  bool emitSaddle(const std::vector<int>& ring, const std::vector<Vector3d>& ringNrm)
  {
    const int n = static_cast<int>(ring.size());
    if (n < 4 || static_cast<int>(ringNrm.size()) != n) return false;

    std::vector<Vector3d> B(n);
    for (int i = 0; i < n; ++i) B[i] = out.V[ring[i]];
    Vector3d Q = Vector3d::Zero();  // shared patch centre = ring centroid
    for (const auto& p : B) Q += p;
    Q /= static_cast<double>(n);

    // Per-boundary Bezier control point: pushed out along the fillet tangent (the
    // inward chord projected off the boundary normal), so the radial curve leaves
    // B[i] tangent to the incident fillet and bulges instead of diving straight for
    // the centre. A degenerate tangent (normal nearly along the chord) falls back to
    // the plain chord. See the arm length below.
    std::vector<Vector3d> M(n);
    for (int i = 0; i < n; ++i) {
      const Vector3d d = Q - B[i];
      const double nl = ringNrm[i].norm();
      Vector3d t = d;
      if (nl > 1e-9) {
        const Vector3d N = ringNrm[i] / nl;
        t = d - d.dot(N) * N;
      }
      const double tl = t.norm();
      t = tl > 1e-9 ? Vector3d(t / tl) : d.normalized();
      // Control-arm length: about half the chord to the centre. A full-chord arm
      // (the natural first guess) overshoots — for a boundary foot far from the
      // centre it carries the radial curve clear across the patch and, at a mixed
      // corner, stands proud of the opposite-sign valley as a needle flap. Half the
      // chord keeps the fillet tangent at the boundary (G1) and a gentle bulge while
      // staying on this foot's own side of the patch.
      M[i] = B[i] + kSaddleArmFrac * d.norm() * t;
    }

    // Pole-free concentric-ring topology: k layers from the boundary ring inward,
    // connected by uniform quad bands, and the innermost ring closed by an ear-clip
    // (NOT a fan to a single apex). Layer l occupies flattened indices [l*n, l*n+n);
    // layer 0 (s=0) is exactly the boundary ring, so the patch welds to the strips.
    // k scales with the arc resolution so the corner keeps pace with high-$fn strips.
    //
    // Each radial column is the half-chord B[i] -> M[i] -> Q quadratic Bezier: its
    // boundary tangent is the fillet's (G1), it bulges gently outward via M[i], and
    // the layers land as nested rings around the centre rather than collapsing to it
    // — the innermost ring stays a small polygon, so there is no interior pole. The
    // half-chord control arm (set where M[i] is built) is what keeps a mixed corner's
    // convex columns from overshooting proud of the concave valley as needle flaps.
    const int k = std::max(2, arcSegs);
    std::vector<Vector3d> V;
    V.reserve(k * n);
    for (int l = 0; l < k; ++l) {
      const double s = static_cast<double>(l) / k;  // 0 at the boundary
      const double b0 = (1 - s) * (1 - s), b1 = 2 * (1 - s) * s, b2 = s * s;
      for (int i = 0; i < n; ++i) V.push_back(b0 * B[i] + b1 * M[i] + b2 * Q);
    }

    std::vector<std::array<int, 3>> F;
    for (int l = 0; l + 1 < k; ++l)
      for (int i = 0; i < n; ++i) {
        const int a = l * n + i, b = l * n + (i + 1) % n;
        const int c = (l + 1) * n + (i + 1) % n, d = (l + 1) * n + i;
        F.push_back({a, b, c});
        F.push_back({a, c, d});
      }

    // Sliver guard. Where the ring turns sharply — a mixed corner's concave arc
    // meeting a convex one — adjacent radial columns can leave with opposite tangents
    // and start near-coincident, leaving a near-zero-area needle triangle (taxonomy
    // defect ⑤). Weld saddle vertices that fall within a small fraction of the radius
    // of each other, but never move a boundary (layer-0, index < n) vertex: those are
    // the exact ring points the strips weld to, so the patch perimeter is untouched
    // and only interior columns collapse. Collapsed triangles become degenerate and
    // out.tri drops them.
    const double weldTol = kSaddleWeldFrac * size;
    std::vector<int> rep(V.size());
    for (int i = 0; i < static_cast<int>(V.size()); ++i) rep[i] = i;
    auto find = [&](int x) { while (rep[x] != x) x = rep[x]; return x; };
    for (int i = 0; i < static_cast<int>(V.size()); ++i) {
      if (find(i) != i) continue;
      for (int j = i + 1; j < static_cast<int>(V.size()); ++j) {
        if (find(j) != j) continue;
        if ((V[i] - V[j]).norm() >= weldTol) continue;
        const bool ib = i < n, jb = j < n;
        if (ib && jb) continue;         // never weld two boundary verts (keep the strip weld)
        if (jb) rep[find(i)] = j;       // keep the boundary vertex as the representative
        else rep[j] = find(i);
      }
    }

    // Verify the weld left a manifold patch, at its edges AND at its vertices.
    //
    // A proximity weld can merge two vertices that are not a collapsible neighbour
    // pair, fusing separate columns so an interior edge ends up shared by three or
    // more surviving triangles — the manifold surgery then rejects the whole blend
    // and returns the model unchanged. Count each surviving triangle's undirected
    // edges; if any is used more than twice the weld is unsafe for this ring.
    //
    // The same fusion can also leave every edge used exactly twice and still be
    // wrong. Where the ring is reentrant its interior layers fold back past
    // themselves, and welding one column's interior onto a point clear across the
    // ring joins two otherwise disjoint sheets of the patch at that point alone: the
    // vertex carries two separate fans. Nothing downstream can see it — the solid is
    // closed, edge-manifold and of the genus it should be, and invalid — so count the
    // fans here too. How far in a column reaches is s = l/k with k = arcSegs, so
    // whether two far-apart columns land inside the fixed tolerance moves with $fn:
    // the same ring pinches at one arc-segment count and is clean at the next.
    //
    // Either failure drops the weld and emits the plain fan. (The saddle is still a
    // valid, if slightly slivered, patch without it.)
    std::map<std::pair<int, int>, std::vector<int>> edgeUse;
    std::map<int, std::vector<int>> incident;
    bool manifold = true;
    for (int f = 0; f < static_cast<int>(F.size()) && manifold; ++f) {
      const int a = find(F[f][0]), b = find(F[f][1]), c = find(F[f][2]);
      if (a == b || b == c || a == c) continue;  // degenerate, dropped by out.tri too
      for (const auto& e : {std::minmax(a, b), std::minmax(b, c), std::minmax(a, c)})
        if (edgeUse[e].push_back(f), edgeUse[e].size() > 2) { manifold = false; break; }
      for (const int v : {a, b, c}) incident[v].push_back(f);
    }
    // One edge-connected fan per vertex: union the triangles at the vertex across the
    // edges that touch it, and require a single component.
    for (auto it = incident.begin(); it != incident.end() && manifold; ++it) {
      std::map<int, int> par;
      for (const int f : it->second) par[f] = f;
      std::function<int(int)> root = [&](int x) {
        while (par[x] != x) x = par[x] = par[par[x]];
        return x;
      };
      for (const auto& [e, fl] : edgeUse) {
        if (e.first != it->first && e.second != it->first) continue;
        for (size_t q = 1; q < fl.size(); ++q)
          if (par.count(fl[0]) && par.count(fl[q])) par[root(fl[0])] = root(fl[q]);
      }
      std::set<int> roots;
      for (const int f : it->second) roots.insert(root(f));
      if (roots.size() > 1) manifold = false;
    }
    if (!manifold)
      for (int i = 0; i < static_cast<int>(V.size()); ++i) rep[i] = i;

    for (const auto& t : F)
      out.tri(out.add(V[find(t[0])]), out.add(V[find(t[1])]), out.add(V[find(t[2])]));

    // Close the innermost ring (layer k-1) by ear-clip rather than a fan to a single
    // apex — the whole point of the pole-free patch. Emit the welded innermost
    // positions to the shared vertex pool (out.add welds them to the quad band's
    // copies), dropping consecutive duplicates the weld may have produced. Ear-clip
    // returns triangles as local indices into this ring; fall back to a centroid fan
    // only if the projection is too tangled to ear-clip, so the patch always closes.
    std::vector<int> inner;
    for (int i = 0; i < n; ++i) {
      const int gi = out.add(V[find((k - 1) * n + i)]);
      if (inner.empty() || inner.back() != gi) inner.push_back(gi);
    }
    if (inner.size() > 1 && inner.front() == inner.back()) inner.pop_back();
    std::vector<std::array<int, 3>> innerTris;
    if (inner.size() >= 3 && earClipRing(inner, innerTris))
      for (const auto& t : innerTris) out.tri(inner[t[0]], inner[t[1]], inner[t[2]]);
    else
      out.fan(inner);
    return true;
  }

  // The edge-local cross-section of selected edge (u,via) at u.
  std::vector<Vector3d> edgeCrossSection(int u, int via) const
  {
    const EdgeKey e{std::min(u, via), std::max(u, via)};
    const auto& ts = adj.at(e);
    return crossSectionEdge(u, ts[0], ts[1], concaveOf.at(e));
  }

  // Whether two cross-section polylines coincide (same points, either order) to
  // the weld tolerance — meaning the two strips ending on them are already sewn
  // together and the vertex between them needs no patch.
  static bool sectionsWeld(const std::vector<Vector3d>& a, const std::vector<Vector3d>& b)
  {
    if (a.size() != b.size() || a.empty()) return false;
    const int n = static_cast<int>(a.size());
    auto same = [](const Vector3d& p, const Vector3d& q) { return (p - q).norm() < kWeldTolMm; };
    bool fwd = true, rev = true;
    for (int i = 0; i < n; ++i) {
      if (!same(a[i], b[i])) fwd = false;
      if (!same(a[i], b[n - 1 - i])) rev = false;
    }
    return fwd || rev;
  }

  void emitCorner(int u)
  {
    // The selected edges meeting at u, as neighbour vertices.
    std::vector<int> selVia;
    for (const auto& [key, ts] : adj)
      if ((key.first == u || key.second == u) && selected.count(key))
        selVia.push_back(key.first == u ? key.second : key.first);
    if (selVia.empty()) { ++cornerCounts.none; return; }

    // A smooth crease passing through u: exactly two selected edges whose
    // edge-local cross-sections coincide. emitEdge already welds their two strips
    // along that shared cross-section (each shared segment carries one quad from
    // either strip), so u is watertight and a patch would double-cover it. This is
    // the rim/pass-through skip — geometric, so it holds whether or not the wall is
    // split into per-facet surfaces (the surface-pair count no longer decides it).
    if (selVia.size() == 2 &&
        sectionsWeld(edgeCrossSection(u, selVia[0]), edgeCrossSection(u, selVia[1]))) {
      ++cornerCounts.weld;
      return;
    }

    const std::vector<std::pair<int, int>> fan = fanAround(u);
    if (fan.empty()) { ++cornerCounts.none; return; }
    const int n = static_cast<int>(fan.size());
    int start = -1;
    for (int i = 0; i < n; ++i)
      if (isSelected(u, fan[i].second)) { start = i; break; }
    if (start < 0) { ++cornerCounts.none; return; }

    // Walk the fan once, building the ring around u: the sector-local inset point
    // wherever a run of triangles turns, and each selected edge's arc interior
    // where the walk crosses it — edge-local throughout, so the ring shares its
    // vertices with the strips exactly. One ring per vertex covers a genuine
    // junction, a strip end against a kept-sharp boundary, and both at once.
    // Each ring point carries the blend surface's own normal there (ringNrm),
    // parallel to ring: a connector (inset) point takes its face normal; an arc
    // point takes (p - C) off its fillet centre. The mixed-sign saddle reads these
    // to leave every boundary point along the fillet's tangent. pushRing keeps all
    // the arrays in lockstep through the same adjacent-duplicate dedup.
    std::vector<int> ring;
    std::vector<Vector3d> ringNrm;
    std::vector<int> ringSign;    // +1 convex arc, -1 concave arc, 0 connector (inset)
    std::vector<char> ringCorner;  // the sector-inset corners (the arc joins)
    auto pushRing = [&](int idx, const Vector3d& nrm, bool corner, int sign) {
      if (!ring.empty() && ring.back() == idx) return;
      ring.push_back(idx);
      ringNrm.push_back(nrm);
      ringSign.push_back(sign);
      ringCorner.push_back(corner ? 1 : 0);
    };
    for (int off = 0; off < n; ++off) {
      const int i = (start + off) % n;
      const int tri = fan[i].first;
      const int via = fan[i].second;  // edge (u,via) leaving this triangle
      pushRing(out.add(insetForTri(u, tri)), m.tris[tri].normal, /*corner=*/true, /*sign=*/0);
      if (isSelected(u, via)) {
        const EdgeKey e{std::min(u, via), std::max(u, via)};
        const auto& ts = adj.at(e);
        const bool concave = concaveOf.at(e);
        std::vector<Vector3d> cs = crossSectionEdge(u, ts[0], ts[1], concave);
        if (ts[0] != tri) std::reverse(cs.begin(), cs.end());  // start on this run's side
        const auto Copt = filletCenter(u, ts[0], ts[1], concave);
        // Interior arc points always; at a mixed corner the two endpoints too — the
        // pulled feet no longer coincide with the sector mitre, so the ring must
        // reach them (the mitre-to-foot connector then welds the surface split and
        // the strip). Elsewhere the endpoints are the mitre already pushed, so they
        // stay excluded and the single-sign cap sees the ring it expects.
        const size_t lo = pulled(u) ? 0 : 1;
        const size_t hi = pulled(u) ? cs.size() : cs.size() - 1;
        for (size_t j = lo; j < hi; ++j) {
          Vector3d nrm = m.tris[tri].normal;  // fallback if the centre is degenerate
          if (Copt) {
            const Vector3d d = cs[j] - *Copt;
            if (d.norm() > 1e-9) nrm = d.normalized();
          }
          pushRing(out.add(cs[j]), nrm, /*corner=*/false, /*sign=*/concave ? -1 : 1);
        }
      }
    }
    if (ring.size() >= 2 && ring.front() == ring.back()) {
      ring.pop_back();
      ringNrm.pop_back();
      ringSign.pop_back();
      ringCorner.pop_back();
    }

    // The walk can come back to a point it has already used: two sections at a
    // crowded corner land on the same place, which is exactly what a mirror-symmetric
    // pair of sections does on its own mirror plane. The ring is then not one loop
    // but several pinched together there, and triangulating it as one loop covers the
    // join twice — a doubled edge where the pinch is a slit, a bow-tie vertex where it
    // is a figure eight. Cut it into its loops instead: each inner loop is closed off
    // on its own here, and the rest carries on to the dispatch below as the corner's
    // ring. A two-point loop is a slit whose one edge the two strips meeting on it
    // already sew, so it closes with nothing.
    for (bool again = true; again;) {
      again = false;
      std::map<int, int> firstAt;
      for (size_t j = 0; j < ring.size(); ++j) {
        const auto [it, ins] = firstAt.try_emplace(ring[j], static_cast<int>(j));
        if (ins) continue;
        const size_t i = static_cast<size_t>(it->second);
        std::vector<int> loop(ring.begin() + i, ring.begin() + j);
        ring.erase(ring.begin() + i, ring.begin() + j);
        ringNrm.erase(ringNrm.begin() + i, ringNrm.begin() + j);
        ringSign.erase(ringSign.begin() + i, ringSign.begin() + j);
        ringCorner.erase(ringCorner.begin() + i, ringCorner.begin() + j);
        if (loop.size() >= 3 && !ringSaddle(loop)) out.fan(loop);
        again = true;
        break;
      }
    }

    std::vector<int> cornerPos;  // ring indices of the sector-inset corners
    for (size_t i = 0; i < ring.size(); ++i)
      if (ringCorner[i]) cornerPos.push_back(static_cast<int>(i));

    // Sign of the selected edges here.
    bool anyConcave = false, anyConvex = false;
    for (const int via : selVia) {
      const EdgeKey e{std::min(u, via), std::max(u, via)};
      (concaveOf.at(e) ? anyConcave : anyConvex) = true;
    }
    const bool mixed = anyConcave && anyConvex;
    const bool junction = selVia.size() >= 2;  // (the weld pass-through already returned)

    // The dispatch, in preference order. Every candidate below closes the SAME ring,
    // so which one is taken is a question of quality alone and never of closure:
    // swapping one for another leaves the corner watertight either way. The order is
    // the standing preference (the exact surface first, the crude fan last) and the
    // first candidate that both builds and lays no flap is taken, so a corner that
    // was already clean costs exactly one construction, as before.
    //
    // Where the preferred construction laps -- itself, or a strip already emitted --
    // the next is tried and the fewest-lapping is kept. That is the ladder's own rule
    // (`better`) brought down to one corner: a patch is only replaced by one measured
    // to be no worse, so no corner can regress, and the whole-mesh census stays the
    // arbiter of the build.
    out.lapIndex();
    const std::size_t mark = out.F.size();
    int bestLaps = std::numeric_limits<int>::max();
    std::vector<std::array<int, 3>> bestF;
    int *bestTally = nullptr;
    auto judge = [&](bool built, int *tally) {
      if (!built) { out.dropFacesSince(mark); return false; }
      const int laps = out.lapsSince(mark);
      if (laps < bestLaps) {
        bestLaps = laps;
        bestF.assign(out.F.begin() + mark, out.F.end());
        bestTally = tally;
      }
      out.dropFacesSince(mark);
      return laps == 0;
    };
    auto settle = [&]() {
      for (const auto& f : bestF) out.tri(f[0], f[1], f[2]);
      out.lapIndex();
      if (bestTally) ++*bestTally;
    };

    // Genuine single-signed junction: tessellate the corner-ball sphere as a
    // rounded cap so it rounds a convex vertex off / fills a concave one — the
    // pole is the sphere point toward the original sharp vertex (its outermost
    // point for a convex corner, the deepest valley point for a concave one).
    // Only for a real junction — a strip end gets a flat cap instead.
    if (!isChamfer && junction && !mixed && ring.size() >= 3) {
      if (auto C = cornerBall(u, anyConcave)) {
        // A trihedral corner (three equal fillet arcs) tessellates as a subdivided
        // spherical triangle — no central pole, facets flowing with the strips — so
        // it does not read as a beaded knuckle. Convex round-over and concave valley
        // are the same construction on the same ball; rings that are not three equal
        // arcs fall through to the pole-fan below.
        if (judge(emitCapTri(ring, cornerPos, *C), &cornerCounts.capTri)) { settle(); return; }
        Vector3d poleUnit = (m.pos[u] - *C).normalized();
        if (poleUnit.squaredNorm() < 0.5) {  // u sits on the centre: fall back to the ring
          Vector3d centroid = Vector3d::Zero();
          for (const int i : ring) centroid += out.V[i];
          centroid /= static_cast<double>(ring.size());
          poleUnit = (centroid - *C).normalized();
        }
        if (judge(emitCap(ring, *C, size, poleUnit), &cornerCounts.cap)) { settle(); return; }
      }
    }

    // Mixed-sign junction: a convex edge and a concave edge share u, so no single
    // ball caps it — fill the ring with a curved saddle membrane instead of a flat
    // patch, removing the creased V-notch the ear-clip left where a concave crease
    // died into a face.
    // Where the two round-overs and the crease close over the vertex as one exact
    // piece of surface, build that piece instead of any membrane.
    if (!isChamfer && junction && mixed && ring.size() >= 4) {
      if (judge(emitCornerTube(ring, ringNrm, ringSign), &cornerCounts.tube)) { settle(); return; }
      if (judge(emitCoonsSaddle(ring, ringNrm, ringSign), &cornerCounts.coons)) { settle(); return; }
      if (judge(emitSaddle(ring, ringNrm), &cornerCounts.saddle)) { settle(); return; }
    }

    // Strip end (a fillet ending against a kept-sharp boundary): close the ring
    // flat by ear-clipping it in its best-fit plane — the perpendicular patch that
    // seals the volume and leaves the kept edges sharp. Falls back to the crude
    // centroid fan if the ring is too tangled to triangulate cleanly (orient()
    // then refuses if that left a hole).
    if (ring.size() >= 4 && judge(ringSaddle(ring), &cornerCounts.flat)) { settle(); return; }
    out.fan(ring);
    judge(true, &cornerCounts.fan);
    settle();
  }

  // Triangulate a (non-planar) ring directly, no central vertex, by ear-clipping
  // in its best-fit plane — the flat perpendicular cap for a strip end. Returns
  // false, leaving the caller to fall back, if no valid ear-clip exists.
  bool ringSaddle(const std::vector<int>& ring)
  {
    std::vector<std::array<int, 3>> tris;
    if (!earClipRing(ring, tris)) return false;
    for (const auto& t : tris) out.tri(ring[t[0]], ring[t[1]], ring[t[2]]);
    return true;
  }

  // A kept surface boundary (a feature edge we did not select — a tessellation
  // seam, a gentle fold, or a crease left sharp by the sign filter) stays sharp,
  // but a nearby selected fillet insets the faces on either side of it, and by
  // different amounts where their sectors carry different setbacks. That splits
  // the shared edge open. Sew it with a flat ribbon between the two faces' inset
  // boundaries — a kept edge is a zero-radius fillet, so this is emitEdge's
  // straight-line counterpart. Away from any fillet both sides inset to the same
  // place and every triangle here degenerates, so the edge stays a plain seam.
  void emitKeptSeam(const EdgeKey& e)
  {
    const auto& ts = adj.at(e);
    const int v = e.first, w = e.second;
    const Vector3d Va = insetForTri(v, ts[0]), Vb = insetForTri(v, ts[1]);
    const Vector3d Wa = insetForTri(w, ts[0]), Wb = insetForTri(w, ts[1]);
    out.tri(Va, Wa, Wb);
    out.tri(Va, Wb, Vb);
  }

  // Build the shared per-vertex cross-section for the pass-through vertices of a
  // smooth crease — the coordinated fix for the junction facet dent. A vertex u
  // qualifies when exactly two selected edges of the same sign meet there and
  // continue nearly straight through it (a crease passing on, not a corner
  // turning or a junction branching). At such a u the fan splits into two runs,
  // one per side of the crease, each bounded by the two crease edges; the run may
  // cross interior tessellation seams (the facets of a curved wall), which is
  // exactly where the per-edge sectors diverge and dent. For each run we compute
  // ONE inset point (mitre of the two bounding crease edges) and ONE averaged
  // seat normal, and stamp them onto every triangle in the run. insetForTri and
  // crossSectionEdge then hand both strips — and the surface pass and any kept
  // seam meeting u — the identical answer, so the strips weld.
  //
  // Corner, junction and mixed-sign vertices are intentionally skipped: they keep
  // the surface-bounded sector seating, whose per-side stop at a sub-feature gap
  // is what closes a genuine junction (spanning it there reopens closure at
  // higher $fn). The near-straight test is what separates the two: a crease
  // continuing straight is safe to span; a corner or a junction branch is not.
  // Whether the corner at u really closes the way emitCornerTube needs it to, read at
  // whatever seat the strips currently carry: the concave section's rolling-ball centre
  // and the two round-overs' centres one ball-diameter apart, all square to the face the
  // two round-overs share, each round-over's foot on that face one radius off its own
  // centre along the face normal, and the concave section already welded to both.
  bool cornerCloses(int u, const std::array<int, 2>& cvx, int y, int sharedSurf,
                    const Vector3d& nf) const
  {
    const double tol = kTubeFitTolFrac * size;
    const auto& tsy = adj.at(EdgeKey{std::min(u, y), std::max(u, y)});
    const auto C0 = filletCenter(u, tsy[0], tsy[1], /*concave=*/true);
    if (!C0) return false;
    const std::vector<Vector3d> csy = crossSectionEdge(u, tsy[0], tsy[1], /*concave=*/true);
    if (static_cast<int>(csy.size()) != arcSegs + 1) return false;
    CornerCanal cc;
    cc.r = size;
    cc.C0 = *C0;
    cc.m = nf;
    cc.e = (m.pos[y] - m.pos[u]).normalized();  // along the crease, into the solid
    if (cc.e.dot(nf) > -1e-6) return false;
    // The concave section must be one circle of the swing's own radius, square to the
    // crease: it is the tube's first cross-section, and the swing runs along it.
    for (const Vector3d& p : csy) {
      const Vector3d w = p - *C0;
      if (std::abs(w.norm() - size) > tol || std::abs(w.dot(cc.e)) > tol) return false;
    }
    // …and it must seat at the swing direction leaning furthest into the face, or the
    // strip overhangs the corner it hands over to.
    const Vector3d s0 = (csy.front() - *C0).normalized(), s1 = (csy.back() - *C0).normalized();
    cc.uTop = s0.dot(nf) >= s1.dot(nf) ? s0 : s1;
    for (const Vector3d& p : csy)
      if (cc.lift((p - *C0).normalized()) > tol) return false;
    for (int r = 0; r < 2; ++r) {
      const auto& ts = adj.at(EdgeKey{std::min(u, cvx[r]), std::max(u, cvx[r])});
      const auto C = filletCenter(u, ts[0], ts[1], /*concave=*/false);
      if (!C) return false;
      const int tf = triInSurface(u, cvx[r], sharedSurf);
      if (tf < 0) return false;
      if ((stripFoot(u, cvx[r], tf) - (*C + size * nf)).norm() > tol) return false;
      const std::vector<Vector3d> cs = crossSectionEdge(u, ts[0], ts[1], /*concave=*/false);
      if (static_cast<int>(cs.size()) != arcSegs + 1) return false;
      // Exactly one end of the round-over section is the crease-side one: the sample the
      // swing starts (or ends) on, one radius off the swing centre for one of the
      // concave section's own two directions.
      auto onSwing = [&](const Vector3d& p) {
        for (const Vector3d& su : {s0, s1})
          if ((cc.at(su, 0.0) - p).norm() < tol) return true;
        return false;
      };
      const bool head = onSwing(cs.front());
      if (head == onSwing(cs.back())) return false;
      const Vector3d w = cs[head ? 0 : arcSegs] - *C0;
      const Vector3d su = (w - w.dot(cc.e) * cc.e).normalized();
      if ((*C - cc.centre(su)).norm() > tol) return false;
      // …and the section must BE the tube's cross-section there, sample for sample.
      for (int j = 0; j <= arcSegs; ++j) {
        const Vector3d q = cc.at(su, static_cast<double>(j) / arcSegs);
        if ((q - cs[head ? j : arcSegs - j]).norm() > tol) return false;
      }
    }
    return std::abs(s0.dot(s1)) < kMinSwingTurnCos;  // the two round-overs must genuinely turn apart
  }

  // Seat the two convex strips of such a corner at their full mitre.
  //
  // The corner between two round-overs closing over a concave crease is one exact piece
  // of surface (emitCornerTube), and that piece begins where each round-over's section
  // centre sits one ball-diameter from the concave section's centre — the full mitre,
  // not the tighter default seat. Seated short, the strips overhang the corner they hand
  // over to, and their end sections are no longer its first and last cross-sections. So
  // reseat them there, and only there: three selected edges, two convex and one concave,
  // closing as cornerCloses reads it. Every other corner keeps the default seat, so
  // nothing else in the blend moves.
  void computeCornerStations()
  {
    if (isChamfer || !pullIn) return;
    for (const int u : mixedVerts) {
      std::vector<int> cv, cc;
      for (const auto& [key, ts] : adj) {
        if ((key.first != u && key.second != u) || !selected.count(key)) continue;
        (concaveOf.at(key) ? cc : cv).push_back(key.first == u ? key.second : key.first);
      }
      if (cv.size() != 2 || cc.size() != 1) continue;
      std::sort(cv.begin(), cv.end());  // positional, so the pair order is not adj's
      const std::array<int, 2> cvx{cv[0], cv[1]};

      // The face both round-overs run along: the one surface incident to both.
      int fx = -1, fz = -1;
      for (const int a : adj.at(EdgeKey{std::min(u, cvx[0]), std::max(u, cvx[0])}))
        for (const int b : adj.at(EdgeKey{std::min(u, cvx[1]), std::max(u, cvx[1])}))
          if (a != b && surfaceOf[a] == surfaceOf[b] && fx < 0) { fx = a; fz = b; }
      if (fx < 0 || m.tris[fx].normal.dot(m.tris[fz].normal) < kSharedFaceCos) continue;

      auto seatFull = [&](int x) {
        const Vector3d eh = (m.pos[x] - m.pos[u]).normalized();
        double smax = -1e30;
        for (const int t : adj.at(EdgeKey{std::min(u, x), std::max(u, x)}))
          smax = std::max(smax, (insetForTri(u, t) - m.pos[u]).dot(eh));
        stationOverride[{u, x}] = smax;
      };
      // Reseat first, then read the corner back at the new seat — every helper below
      // goes through pullStation — and back the reseat out if it does not close.
      seatFull(cvx[0]);
      seatFull(cvx[1]);
      if (cornerCloses(u, cvx, cc[0], surfaceOf[fx], m.tris[fx].normal)) continue;
      // Where the shared face is raked the tube's concave boundary is a slanted cut of
      // the crease's own fillet, and the strip has to seat at the deepest point of it or
      // it overhangs the corner. That is a second reseat, tried only after the plain one
      // has failed, so a corner that already closed keeps the seat it closed on.
      seatFull(cc[0]);
      if (!cornerCloses(u, cvx, cc[0], surfaceOf[fx], m.tris[fx].normal))
        for (const int x : {cvx[0], cvx[1], cc[0]}) stationOverride.erase({u, x});
    }
  }

  void prepareShared()
  {
    // A crease continues through u when its two edges leave nearly opposite —
    // the same angle the selection walk uses to follow a crease
    // (kPassThroughMaxTurnDeg, kept in step with kCreaseFollowMaxTurnDeg). A
    // sharper turn is a corner (needs a cap, must not be forced to weld); a wider
    // fan is a junction (three-plus selected edges, filtered by the count below).
    const double cosOpp = -std::cos(kPassThroughMaxTurnDeg * M_PI / 180.0);

    std::set<int> verts;
    for (const auto& e : selected) { verts.insert(e.first); verts.insert(e.second); }
    for (const int u : verts) {
      // The selected edges meeting at u.
      std::vector<int> sv;
      for (const auto& [key, ts] : adj)
        if ((key.first == u || key.second == u) && selected.count(key))
          sv.push_back(key.first == u ? key.second : key.first);
      if (sv.size() != 2) continue;  // strip end, junction — leave to emitCorner
      const EdgeKey e0{std::min(u, sv[0]), std::max(u, sv[0])};
      const EdgeKey e1{std::min(u, sv[1]), std::max(u, sv[1])};
      if (concaveOf.at(e0) != concaveOf.at(e1)) continue;  // mixed-sign — leave alone
      const Vector3d d0 = (m.pos[sv[0]] - m.pos[u]).normalized();
      const Vector3d d1 = (m.pos[sv[1]] - m.pos[u]).normalized();
      if (d0.dot(d1) > cosOpp) continue;  // a corner turning, not a crease passing

      const std::vector<std::pair<int, int>> fan = fanAround(u);
      const int n = static_cast<int>(fan.size());
      if (n == 0) continue;  // not a clean manifold fan
      // Fan positions of the two crease edges. fan[i].second is the edge between
      // triangle fan[i] and fan[i+1]; a crease edge at position p thus separates
      // the two runs, with fan[p+1] on one side and fan[p] on the other.
      int pa = -1, pb = -1;
      for (int i = 0; i < n; ++i) {
        if (fan[i].second == sv[0]) pa = i;
        else if (fan[i].second == sv[1]) pb = i;
      }
      if (pa < 0 || pb < 0) continue;

      // Stamp one run of triangles (fan[from..to], cyclic inclusive) with the
      // mitre of its two bounding crease edges and the run's averaged normal.
      auto stampRun = [&](int from, int to, int viaStart, int triStart, int viaEnd, int triEnd) {
        std::vector<int> tris;
        Vector3d nsum = Vector3d::Zero();
        for (int i = from;; i = (i + 1) % n) {
          tris.push_back(fan[i].first);
          nsum += m.tris[fan[i].first].normal;
          if (i == to) break;
        }
        const Vector3d nSide =
          nsum.norm() > 1e-12 ? Vector3d(nsum.normalized()) : m.tris[fan[from].first].normal;
        auto offsetLine = [&](int x, int tside, Vector3d& base, Vector3d& dir) {
          const EdgeKey e{std::min(u, x), std::max(u, x)};
          base = m.pos[u] + setback(e) * perpInto(u, x, tside);
          dir = (m.pos[x] - m.pos[u]).normalized();
        };
        Vector3d b1, dd1, b2, dd2;
        offsetLine(viaStart, triStart, b1, dd1);
        offsetLine(viaEnd, triEnd, b2, dd2);
        const Vector3d T =
          mitre(m.pos[u], b1, dd1, setback(EdgeKey{std::min(u, viaStart), std::max(u, viaStart)}),
                b2, dd2, setback(EdgeKey{std::min(u, viaEnd), std::max(u, viaEnd)}));
        for (const int t : tris) {
          insetOverride[{u, t}] = T;
          normalOverride[{u, t}] = nSide;
        }
      };
      // Run from just past sv[0] to sv[1], bounded by sv[0]@fan[pa+1] and
      // sv[1]@fan[pb]; then the complementary run the other way.
      stampRun((pa + 1) % n, pb, sv[0], fan[(pa + 1) % n].first, sv[1], fan[pb].first);
      stampRun((pb + 1) % n, pa, sv[1], fan[(pb + 1) % n].first, sv[0], fan[pa].first);
    }
  }

  // Re-emit every surface triangle with its boundary vertices set back to their
  // inset positions (interior vertices are unchanged), then the edge strips, the
  // kept-seam ribbons, and the corner patches.
  void run()
  {
    computeMixedVerts();
    prepareShared();
    computeCornerStations();
    repairInsetFolds();
    trimFootprintSpikes();
    // The size gate is read at the seats the trim has settled on, so a spike it has
    // already collapsed is not mistaken for a face the setbacks have consumed. A
    // face that is over-round is not emitted at all: the caller refuses those creases
    // and builds again without them.
    if (gateSize) {
      overRound = overRoundEdges();
      if (!overRound.empty()) return;
    }
    flipInvertedInsets();
    // Each planar face is emitted as one footprint — its own retreated boundary,
    // re-triangulated to fit it. Where that cannot be vouched for, and on every
    // curved surface (whose facets are surfaces of their own and carry no interior
    // diagonal to get wrong), the face falls back to the per-triangle inset: each
    // vertex on the side triangle t sits on, sector-local, so a facet of a split
    // wall sets back along its own sector rather than mitring across a surface it is
    // no longer grouped with, and re-triangulated at a mixed corner to carry the
    // pulled-in strip feet.
    //
    // A surface that is not planar as a whole is tried again run by coplanar run
    // before the per-triangle fallback. A tangent junction groups the cylinder's
    // wall with the face it runs into, so the surface has two planes and the
    // footprint declined the pair outright — leaving the per-triangle path to set
    // back the razor needles the union leaves along the tangency line when no model
    // vertex lands on it. Each needle's corners then retreat a whole radius in
    // directions that bear no relation to its own width, and the retreated face laps
    // over itself. Re-cutting each run from its own footprint drops the source
    // triangulation, needles and all. The runs meet along a seam that is not a
    // crease, so both sides seat it on the same sector-local insets and weld.
    for (const auto& [S, tris] : surfaceGroups()) {
      if (emitSurfacePatch(tris)) continue;
      const auto runs = coplanarRuns(tris);
      if (runs.size() > 1) {
        std::vector<int> left;
        for (const auto& run : runs)
          if (!emitSurfacePatch(run.second))
            left.insert(left.end(), run.second.begin(), run.second.end());
        if (left.empty()) continue;
        for (const int t : left) emitSurfaceTri(t);
        continue;
      }
      for (const int t : tris) emitSurfaceTri(t);
    }
    for (const auto& e : selected) emitEdge(e);
    for (const auto& e : feature)
      if (!selected.count(e)) emitKeptSeam(e);
    std::set<int> corners;
    for (const auto& e : selected) {
      corners.insert(e.first);
      corners.insert(e.second);
    }
    for (const int u : corners) emitCorner(u);
  }
};

}  // namespace

std::shared_ptr<const Geometry> buildBlend(
  const FilletNode& node, const std::shared_ptr<const ManifoldGeometry>& target,
  const std::shared_ptr<const ManifoldGeometry>& brush)
{
  const bool isChamfer = (node.type == FilletType::CHAMFER);
  const char *sizeName = isChamfer ? "setback" : "radius";

  if (!target || target->isEmpty()) {
    LOG(message_group::Warning, node.modinst->location(), "",
        "%1$s: nothing to blend (child 0 is empty)", node.name());
    return target;
  }
  if (!node.convex && !node.concave) {
    LOG(message_group::Warning, node.modinst->location(), "",
        "%1$s: convex = false and concave = false select no edges; the model is returned unchanged",
        node.name());
    return target;
  }
  if (!(node.size > 0)) {
    LOG(message_group::Warning, node.modinst->location(), "", "%1$s: %2$s must be positive",
        node.name(), sizeName);
    return target;
  }

  const MergedMesh m0base = mergeMesh(target->getManifold().GetMeshGL64());
  const double thresholdDeg = node.min_angle >= 0 ? node.min_angle : kDefaultCreaseThresholdDeg;

  // The selection brush (the union of the node's brush children), as the solid a
  // crease edge is tested against: an edge is blended only where its spine lies
  // inside the brush. Absent when the node has no brushes (the whole model).
  std::optional<BrushVolume> brushVol;
  if (brush && !brush->isEmpty()) brushVol.emplace(brush->getManifold().GetMeshGL64());

  // One blend attempt on a given mesh. The whole build is mesh-dependent, so it
  // is factored out to be runnable twice: once on the subdivided mesh, once on
  // the raw one if subdivision produced a non-manifold result (below).
  enum class Status { Ok, Empty, NoSelection, Holed, OverRound };
  struct Attempt
  {
    Status status = Status::Empty;
    std::unique_ptr<PolySet> geom;
    std::size_t selected = 0, nConcave = 0, nConvex = 0, refused = 0;
    std::size_t verts = 0, tris = 0;
    int boundary = 0, nonman = 0, folds = 0;
    // Whether this attempt was built on a mesh whose long creases were split. Two
    // attempts are only comparable by fold count when this agrees: an unsplit build
    // carries a fraction of the stations and so a fraction of the chances to fold,
    // and would win a fold comparison by having less surface rather than better.
    bool sub = false;
    Blender::CornerCounts corners;
  };
  // Is x a better attempt than y: first that it built at all, then the along-sweep
  // stations, then the fold census, then how many creases it managed to blend.
  auto better = [](const Attempt& x, const Attempt& y) {
    if (x.status != Status::Ok) return false;
    if (y.status != Status::Ok) return true;
    if (x.sub != y.sub) return x.sub;  // never trade the along-sweep stations for folds
    if (x.folds != y.folds) return x.folds < y.folds;
    return x.refused < y.refused;  // at equal quality, blend the most creases
  };
  const std::map<int, EdgeKey> stationOfNone;
  // One pass of one attempt: `refuse` are the creases an earlier pass of the same
  // attempt found over-round, left sharp here; `overRound`, when given, receives the
  // ones this pass found (and then nothing was emitted).
  auto buildPass = [&](const MergedMesh& m, double surfaceThresholdDeg, bool pullIn,
                       double stationFrac, bool turnPull, bool sub,
                       const std::map<int, EdgeKey>& stationOf,
                       const std::set<EdgeKey>& refuse,
                       std::set<EdgeKey>* overRound) -> Attempt {
    const std::map<EdgeKey, std::vector<int>> adj = buildEdgeAdjacency(m.tris);
    // Group surfaces by near-tangency, not by the feature threshold: a sub-crease
    // seam that is not near-tangent (a tee's tangent gap) must stay a surface
    // boundary so the two walls keep distinct ids and the junction cross-section
    // does not degenerate.
    const std::vector<int> surfaceOf = smoothSurfaces(m, adj, surfaceThresholdDeg);
    Blender b{m,            adj,       surfaceOf, node.size, isChamfer,
              thresholdDeg, node.discretizer};
    b.pullIn = pullIn;
    b.stationFrac = stationFrac;
    b.turnPull = turnPull;
    b.gateSize = overRound != nullptr;
    // Uniform arc tessellation: a quarter-turn's worth of segments from the
    // discretizer, applied to every cross-section regardless of its subtended
    // angle. A fillet crease whose dihedral varies (an oblique elliptical seam)
    // would otherwise give adjacent cross-sections unequal point counts and tear
    // the strip. A constant count is $fn-driven (classification itself stays
    // mesh-only) yet keeps every strip closable.
    if (!isChamfer) {
      const int full = node.discretizer.getCircularSegmentCount(node.size, 360.0).value_or(16);
      b.arcSegs = std::max(2, (full + 3) / 4);
    }
    Attempt a;
    // Selection follows the crease. A crease is a connected chain of edges that
    // each turn more than the surface threshold (so each is a surface boundary);
    // the whole chain is eligible to be blended when — and only when — some edge
    // on it turns past the feature threshold (min_angle). Following the chain
    // continues along the most-collinear neighbour and stops where the crease
    // drops below the surface threshold, so a tee's intersection loop is blended
    // all the way round (its shallow tangent sides included, no gap) while a
    // tessellation seam or a gentle fold — a chain that never reaches the feature
    // threshold — is left sharp. This is the one place the two thresholds meet:
    // min_angle decides eligibility, the surface threshold bounds the crease.
    const CreaseSelection sel = selectCreaseEdges(m, adj, thresholdDeg, surfaceThresholdDeg);
    for (const auto& [key, ec] : sel.crease) b.feature.insert(key);
    for (const auto& key : sel.eligible) {
      const EdgeClass& ce = sel.crease.at(key);
      const bool want = ce.concave ? node.concave : node.convex;
      if (!want) continue;
      // Brush clips the selection to a region by intersecting the spine, not the
      // blend volume ($fn-invariant): an edge is blended where its midpoint lies
      // inside the brush. Edges the brush excludes stay sharp and are sewn by the
      // kept-seam ribbon and the strip-end cap, exactly like the sign filter's.
      //
      // The question is asked of the model's own edge, not of the station it was cut
      // into. The along-sweep floor splits a long crease into pieces whose count
      // comes from the blend size, and asking each piece separately hands the brush
      // a resolution nobody chose: a brush that covers a few millimetres at the end
      // of an edge it was never aimed at takes the whole station containing them, so
      // a corner the brush merely reaches past acquires a round-over running a
      // station deep into two edges that were meant to stay sharp -- and the depth
      // moves if the station spacing does. Resolving the piece back to the crease it
      // was cut from makes the brush mean what it says and makes it station-
      // independent, which is the same argument that keeps it off the blend volume.
      if (brushVol) {
        EdgeKey ask = key;
        for (const int v : {key.first, key.second}) {
          const auto it = stationOf.find(v);
          if (it != stationOf.end()) { ask = it->second; break; }
        }
        if (!brushVol->contains(0.5 * (m.pos[ask.first] + m.pos[ask.second]))) continue;
      }
      // A crease the size gate refused on an earlier pass stays sharp: it is still a
      // feature edge, so the kept-seam ribbon sews it exactly as a brush-excluded or
      // sign-filtered one.
      if (refuse.count(key)) continue;
      b.selected.insert(key);
      b.concaveOf[key] = ce.concave;
      if (ce.concave) ++a.nConcave; else ++a.nConvex;
    }
    a.selected = b.selected.size();
    a.refused = refuse.size();
    if (b.selected.empty()) {
      a.status = refuse.empty() ? Status::NoSelection : Status::OverRound;
      return a;
    }
    b.run();
    if (overRound && !b.overRound.empty()) {
      *overRound = b.overRound;
      a.status = Status::OverRound;
      return a;
    }
    a.corners = b.cornerCounts;
    a.boundary = b.out.orient();
    a.nonman = b.out.nonManifoldEdges();
    if (b.out.F.empty()) { a.status = Status::Empty; return a; }
    // A hole or a self-overlap both make an invalid solid; refuse either rather
    // than emit it (a false refusal is the safe error, promise 1).
    if (a.boundary > 0 || a.nonman > 0) { a.status = Status::Holed; return a; }
    a.status = Status::Ok;
    a.sub = sub;
    a.folds = b.out.foldPairs();
    a.verts = b.out.V.size();
    a.tris = b.out.F.size();
    a.geom = b.out.build();
    return a;
  };

  // One build, with or without the size gate. Gated, a pass that finds over-round
  // creases has emitted nothing: they are dropped from the selection and the build is
  // made again without them. Refusing only ever removes setback, so each pass has
  // strictly less to consume than the last and the sweep converges; the final pass
  // runs ungated so a mesh still over-round at the cap is emitted rather than lost.
  auto buildOn = [&](const MergedMesh& m, double surfaceThresholdDeg, bool pullIn,
                     double stationFrac, bool turnPull, bool sub,
                     const std::map<int, EdgeKey>& stationOf, bool gate) -> Attempt {
    std::set<EdgeKey> refuse;
    for (int pass = 0;; ++pass) {
      std::set<EdgeKey> more;
      const bool last = !gate || pass + 1 >= kOverRoundPasses;
      Attempt a = buildPass(m, surfaceThresholdDeg, pullIn, stationFrac, turnPull, sub, stationOf,
                            refuse, last ? nullptr : &more);
      if (more.empty()) return a;
      refuse.insert(more.begin(), more.end());
    }
  };

  // Prefer the tight pulled-in seating (round-overs flow into the corner); if it
  // leaves the mesh open on an oblique or crowded corner it cannot weld, fall back
  // to the full-mitre pull-in (the un-blade seat that only ever shrinks the patch),
  // then to the baseline mitre seating — each on the subdivided mesh, then on the
  // raw one if subdivision itself went non-manifold. Each tier is never worse than
  // the next, so a corner that cannot take the tight seat gets the full mitre, and
  // one that cannot take the pull-in at all gets exactly the baseline blend.
  //
  // A tier that closes is not the same as a tier that is right, and the difference
  // is exactly the fold: a seat can leave every strip welded and two of them lapped.
  // So the tiers are also walked when the first one closes but folds, and the best
  // is kept. A clean first build ends the search immediately, which is every model
  // that has no crowded corner in it, so the common case still costs one build.
  auto ladder = [&](const MergedMesh& mm, const std::map<int, EdgeKey>& stationOf,
                    const MergedMesh& unsplit, bool splitFallback, bool gate) -> Attempt {
    Attempt r = buildOn(mm, kDefaultSurfaceThresholdDeg, /*pullIn=*/true, kPullStationFrac, false,
                        true, stationOf, gate);
    if (r.status != Status::Ok || r.folds > 0) {
      Attempt t = buildOn(mm, kDefaultSurfaceThresholdDeg, true, 1.0, false, true, stationOf, gate);
      if (better(t, r)) r = std::move(t);
    }
    if (r.status != Status::Ok || r.folds > 0) {
      Attempt t = buildOn(mm, kDefaultSurfaceThresholdDeg, false, 1.0, false, true, stationOf, gate);
      if (better(t, r)) r = std::move(t);
    }
    if (r.status != Status::Ok || r.folds > 0) {
      Attempt t =
        buildOn(mm, kDefaultSurfaceThresholdDeg, true, kPullStationFrac, true, true, stationOf, gate);
      if (better(t, r)) r = std::move(t);
    }
    if (splitFallback && (r.status != Status::Ok || r.folds > 0)) {
      Attempt t =
        buildOn(unsplit, kDefaultSurfaceThresholdDeg, false, 1.0, false, /*sub=*/false, {}, gate);
      if (better(t, r)) r = std::move(t);
    }
    return r;
  };

  // One whole search, from one reading of the swallowed vertices. `curvedFans` is
  // the offer dissolveSwallowedVertices makes on a smooth wall: it is an
  // approximation of the wall inside the band the blend covers, so it is judged
  // rather than assumed -- the caller below runs this again without it and keeps
  // whichever folded less, exactly as the sliver weld is judged.
  bool cutCurvedFans = false;
  auto searchFrom = [&](bool curvedFans) -> Attempt {
    MergedMesh m0 = m0base;
    std::map<int, EdgeKey> stationOfSub, stationOfRawSub;
    // Along-sweep station floor (cap kAlongSweep * size, named at file scope with
    // the angle caps): split long selected creases so a straight fillet holds a
    // constant profile instead of tapering. Where subdivision densifies a
    // degenerate region (two fillets colliding along an exact tangency line) it can
    // turn a marginally-valid over-size case non-manifold; there, fall back to the
    // un-subdivided build, which is never worse than before this floor existed.
    // The edges to densify are exactly the ones buildOn will blend: the eligible
    // set computed on the raw mesh at the same two thresholds, tool-sign filtered.
    // Taking it from selectCreaseEdges (rather than the feature key alone) means a
    // shallow tangent stretch carried into the selection by crease-following — a
    // tee's tangent sides — is given stations too, so a straight fillet there holds
    // its profile instead of tapering to the corner-distorted ends. The same set says
    // which face vertices the blend swallows, dissolved off both meshes before either
    // build so the raw fallback carries the fix too.
    MergedMesh mSub, mRaw;
    {
      const std::map<EdgeKey, std::vector<int>> adj0 = buildEdgeAdjacency(m0.tris);
      const CreaseSelection sel0 =
        selectCreaseEdges(m0, adj0, thresholdDeg, kDefaultSurfaceThresholdDeg);
      std::set<EdgeKey> toSplit;
      for (const auto& key : sel0.eligible)
        if (sel0.crease.at(key).concave ? node.concave : node.convex) toSplit.insert(key);
      cutCurvedFans |=
        dissolveSwallowedVertices(m0, sel0.crease, toSplit, node.size, isChamfer, curvedFans);
      // The sliver weld is a tier of its own, and the mesh it was applied to is kept:
      // it removes a blade the tessellation put there, but putting the turn it carried
      // on one vertex can leave a corner sharper than the patches can close, and then
      // the whole blend would be refused for a fault that was two lapped triangles.
      // Keep the unwelded mesh so that case falls back to it rather than to nothing.
      mRaw = m0;
      weldSliverCreaseEdges(m0, toSplit, kSliverCreaseFrac * node.size);
      mSub = m0;
      subdivideLongCreaseEdges(mSub, toSplit, kAlongSweep * node.size, &stationOfSub);
    }
    const bool didSubdivide = mSub.tris.size() != m0.tris.size();
    const bool didWeld = mRaw.tris.size() != m0.tris.size();

    // The sliver weld is judged, not assumed. Removing a blade the tessellation put
    // there usually removes the fold it caused, but putting the turn that blade
    // carried onto one vertex can leave a corner the patches close worse than they
    // closed around the sliver -- or cannot close at all, in which case the ladder
    // above has already refused the whole blend for two lapped triangles. So the
    // unwelded mesh is built too and the better of the two is kept, where better is
    // first "it built" and then "it has fewer folds". A fold is invisible to every
    // other check here (closed, edge-manifold, same genus, and still wrong), so
    // counting them is the only way this choice can be made at all; ties go to the
    // unwelded mesh, which is the one that moved no geometry.
    MergedMesh mRawSub;
    if (didWeld) {
      mRawSub = mRaw;
      const std::map<EdgeKey, std::vector<int>> adjR = buildEdgeAdjacency(mRaw.tris);
      const CreaseSelection selR =
        selectCreaseEdges(mRaw, adjR, thresholdDeg, kDefaultSurfaceThresholdDeg);
      std::set<EdgeKey> split;
      for (const auto& key : selR.eligible)
        if (selR.crease.at(key).concave ? node.concave : node.convex) split.insert(key);
      subdivideLongCreaseEdges(mRawSub, split, kAlongSweep * node.size, &stationOfRawSub);
    }
    auto walk = [&](bool gate) -> Attempt {
      Attempt r = ladder(mSub, stationOfSub, m0, didSubdivide, gate);
      if (!didWeld) return r;
      Attempt u = ladder(mRawSub, stationOfRawSub, mRaw, true, gate);
      // Ties go to the unwelded mesh: it is the one that moved no geometry.
      if (u.status == Status::Ok &&
          (r.status != Status::Ok || (u.sub == r.sub && u.folds <= r.folds) || (u.sub && !r.sub)))
        return u;
      return r;
    };

    Attempt a = walk(/*gate=*/false);
    // The size gate is the last tier of all, for the same reason the seats above are
    // tiered: leaving a crease sharp is a real loss, and it is only worth taking where
    // the blend it replaces was folded (or would not close at all). A model that came
    // out clean never reaches this and is untouched by the gate; one that did not is
    // built again with the over-round creases refused, and the refusal is kept only if
    // it laid fewer flaps than the blend it replaced. `better` breaks a fold tie by
    // refusing less, so a refusal that bought nothing is dropped.
    if (a.status != Status::Ok || a.folds > 0) {
      Attempt g = walk(/*gate=*/true);
      if (better(g, a)) a = std::move(g);
    }
    return a;
  };

  Attempt a = searchFrom(/*curvedFans=*/true);
  // The smooth-wall cut, judged. It removes the wall vertices a curved retreat
  // swallows and cannot leave -- their fans are not flat, so the exact dissolve
  // declines them and the face triangles around them come back turned over. The
  // cut is not exact, though: on a fine tessellation it can run ring after ring
  // and hand the blend a coarser wall than it started with. So the search is made
  // again on the wall nobody cut, and the fewer-folds build wins; a model where no
  // smooth fan was cut at all never pays for the second search.
  if (cutCurvedFans && (a.status != Status::Ok || a.folds > 0)) {
    Attempt k = searchFrom(/*curvedFans=*/false);
    if (better(k, a)) a = std::move(k);
  }

  switch (a.status) {
    case Status::Empty:
      return target;
    case Status::NoSelection:
      LOG(message_group::Warning, node.modinst->location(), "",
          "%1$s: no selected edge turns more than %2$.1f deg; the model is returned unchanged",
          node.name(), thresholdDeg);
      return target;
    case Status::Holed:
      // The surgery left a hole or a self-overlap — invalid. Say so and hand back
      // the model unchanged rather than a torn solid: a false refusal is the safe
      // error.
      LOG(message_group::Warning, node.modinst->location(), "",
          "%1$s: the blend left %2$d open edge(s) and %3$d non-manifold edge(s); the model is "
          "returned unchanged",
          node.name(), a.boundary, a.nonman);
      return target;
    case Status::OverRound:
      // Every selected crease stands on a face narrower than the setbacks marching
      // in from its two sides. There is no blend of this size to build.
      LOG(message_group::Warning, node.modinst->location(), "",
          "%1$s: %2$s %3$g does not fit the geometry — the faces it retreats across are "
          "narrower than the setback it asks of them; the model is returned unchanged",
          node.name(), sizeName, node.size);
      return target;
    case Status::Ok:
      break;
  }
  if (a.refused > 0)
    LOG(message_group::Warning, node.modinst->location(), "",
        "%1$s: %2$s %3$g does not fit the local geometry at %4$d crease edge(s) — the face "
        "between their round-overs is narrower than the two setbacks; those edges are left "
        "sharp and the rest of the model is blended",
        node.name(), sizeName, node.size, static_cast<int>(a.refused));

  LOG(message_group::Echo, node.modinst->location(), "",
      "%1$s: %2$s %3$g blended %4$d edge(s) (%5$d concave, %6$d convex); %7$d verts, %8$d tris",
      node.name(), sizeName, node.size, static_cast<int>(a.selected),
      static_cast<int>(a.nConcave), static_cast<int>(a.nConvex), static_cast<int>(a.verts),
      static_cast<int>(a.tris));
  if (a.corners.total() > 0)
    LOG(message_group::Echo, node.modinst->location(), "",
        "%1$s: corners tube=%2$d capTri=%3$d cap=%4$d coons=%5$d saddle=%6$d flat=%7$d fan=%8$d "
        "weld=%9$d none=%10$d",
        node.name(), a.corners.tube, a.corners.capTri, a.corners.cap, a.corners.coons,
        a.corners.saddle, a.corners.flat, a.corners.fan, a.corners.weld, a.corners.none);
  return std::move(a.geom);
}
