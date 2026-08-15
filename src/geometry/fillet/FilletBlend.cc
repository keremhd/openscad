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

// A crease passes THROUGH a vertex — rather than turning a corner or branching a
// junction — when its two edges leave nearly opposite, within this bound. It is
// the same angle the follow walk uses, and the shared cross-section that welds
// the two strips at such a vertex depends on the two staying in step.
inline constexpr double kPassThroughMaxTurnDeg = 40.0;

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
  double q = 1e6;  // weld quantum: 1e-6 mm (the stated weld tolerance)

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
  constexpr double kCreaseStraightDeg = 20.0;
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
    // (other than back to v) marks it a seam too.
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
void subdivideLongCreaseEdges(MergedMesh& m, const std::set<EdgeKey>& edges, double cap)
{
  if (!(cap > 0) || edges.empty()) return;
  struct Split { int a, b, parts; };
  std::vector<Split> todo;
  for (const EdgeKey& key : edges) {
    const double len = (m.pos[key.second] - m.pos[key.first]).norm();
    if (len <= cap) continue;
    int parts = static_cast<int>(std::ceil(len / cap));
    parts = std::min(parts, 256);  // backstop against a pathological count
    todo.push_back({key.first, key.second, parts});
  }
  // Vertex ids are only ever appended, so the endpoints collected above stay
  // valid; splitEdge rescans the current triangle list, so shared triangles
  // split by an earlier edge are handled correctly.
  for (const Split& s : todo) splitEdge(m, s.a, s.b, s.parts);
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

  // Vertices where selected edges of BOTH signs meet — the mixed corners whose
  // strips otherwise blade. Populated by computeMixedVerts() before any emit.
  // Everything keyed off this set (the pulled-in cross-section, the surface
  // re-triangulation, the ring's arc endpoints) is inert at every other vertex,
  // so single-sign caps and pass-through welds are untouched.
  std::set<int> mixedVerts;

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
    return lineIntersect(b1, d1, b2, d2);
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
    return lineIntersect(b1, d1, b2, d2);
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
  // into the corner instead of necking a full radius short. Clamped to keep a
  // straight middle on a short corner-to-corner edge.
  double pullStation(int u, int x) const
  {
    const auto& ts = adj.at(EdgeKey{std::min(u, x), std::max(u, x)});
    const Vector3d eh = (m.pos[x] - m.pos[u]).normalized();
    double smax = -1e30, smin = 1e30;
    for (const int t : ts) {
      const double d = (insetForTri(u, t) - m.pos[u]).dot(eh);
      smax = std::max(smax, d);
      smin = std::min(smin, d);
    }
    // Seat between the two face mitres: the deepest (smax) is the full un-blade
    // station that retreats the strip a whole radius, the shallowest (smin) sits at
    // or behind the corner. stationFrac interpolates toward smax; pulling in from it
    // lets the convex round-overs flow into the corner (the concave fillet recedes
    // in 3D, so the planar deepest-mitre clearance is an over-estimate). A symmetric
    // corner (the concave crease, both mitres equal) is a no-op — it keeps its full
    // mitre and does not overshoot. The along-sweep clamp still keeps a straight
    // middle on a short edge and holds crowded/degenerate corners at their tiny
    // station, so the tighter seat is a no-op there too.
    const double s = smin + stationFrac * (smax - smin);
    return std::min(s, 0.45 * (m.pos[x] - m.pos[u]).norm());
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
  // the point crossSectionEdge sweeps its arc about. One radius off the canonical
  // side along its sector normal (the plain rolling-ball construction, so a planar
  // crease builds the same arc a surface-based inset would); the canonical side is
  // the sector with the numerically smaller averaged normal, chosen so the result
  // is independent of which incident triangle adj happened to list first. Exposed
  // so the mixed-corner saddle can read each ring point's fillet-surface normal
  // as (p - C): that normal is what lets the patch continue the roll's own tangent
  // instead of relaxing to a caving minimal surface.
  std::optional<Vector3d> filletCenter(int u, int t0, int t1, bool concave) const
  {
    const int x = mixedVerts.count(u) ? farOf(u, t0, t1) : -1;
    const Vector3d Ta = x >= 0 ? stripFoot(u, x, t0) : insetForTri(u, t0);
    const Vector3d Tb = x >= 0 ? stripFoot(u, x, t1) : insetForTri(u, t1);
    const auto sa = sectorOf(u, t0);
    const auto sb = sectorOf(u, t1);
    if (!sa || !sb) return std::nullopt;
    Vector3d nA = sa->navg, nB = sb->navg;
    if (const auto it = normalOverride.find({u, t0}); it != normalOverride.end()) nA = it->second;
    if (const auto it = normalOverride.find({u, t1}); it != normalOverride.end()) nB = it->second;
    const bool aFirst = std::make_tuple(nA.x(), nA.y(), nA.z()) <=
                        std::make_tuple(nB.x(), nB.y(), nB.z());
    const Vector3d nCen = aFirst ? nA : nB;
    const Vector3d Tcen = aFirst ? Ta : Tb;
    const double sgn = concave ? size : -size;
    return Vector3d(Tcen + sgn * nCen);
  }

  std::vector<Vector3d> crossSectionEdge(int u, int t0, int t1, bool concave) const
  {
    const int x = mixedVerts.count(u) ? farOf(u, t0, t1) : -1;
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
        if (e.dot(n) > 0.999) dup = true;
      if (!dup) normals.push_back(n);
    }
    // Greedily pick three mutually independent normals.
    std::vector<Vector3d> basis;
    for (const auto& n : normals) {
      if (basis.empty()) { basis.push_back(n); continue; }
      if (basis.size() == 1) {
        if (basis[0].cross(n).norm() > 0.1) basis.push_back(n);
        continue;
      }
      if (std::abs(basis[0].cross(basis[1]).dot(n)) > 0.1) { basis.push_back(n); break; }
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
  void emitCap(const std::vector<int>& ring, const Vector3d& C, double r,
               const Vector3d& poleUnit)
  {
    const int n = static_cast<int>(ring.size());
    if (n < 3) return;
    const int L = std::max(1, arcSegs);
    std::vector<Vector3d> u(n);
    for (int i = 0; i < n; ++i) {
      const Vector3d d = out.V[ring[i]] - C;
      const double len = d.norm();
      u[i] = len > 1e-12 ? Vector3d(d / len) : poleUnit;
    }
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
  }

  // Tessellate a convex trihedral corner as a subdivided spherical triangle rather
  // than emitCap's pole-and-rings fan. A genuine convex box corner is three fillet
  // arcs meeting at three sector corners, all on one sphere about C. A
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
  // arcs, which is every real convex box/plate corner.
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

  // One surface triangle, re-triangulated where it meets a mixed corner. Away from
  // a mixed corner it is just the three inset vertices (unchanged). At a mixed
  // corner u the strip along a selected edge (u,x) is pulled in to a common station
  // (stripFoot), which lands past that face's mitre on the shorter-mitre face; the
  // face must carry the strip's foot as a boundary vertex there, or the strip's
  // tangent point T-junctions the mitre and opens a hole. Insert each such foot on
  // the inset edge between the corner's mitre and x's inset, in order from the
  // corner outward, then ear-clip the resulting planar polygon. Feet that coincide
  // with the mitre (the furthest-mitre face) add nothing.
  void emitSurfaceTri(int ti)
  {
    const auto& v = m.tris[ti].v;
    const Vector3d P0 = insetForTri(v[0], ti), P1 = insetForTri(v[1], ti),
                   P2 = insetForTri(v[2], ti);
    const std::array<Vector3d, 3> P{P0, P1, P2};
    bool anyCorner = false;
    for (const int k : v)
      if (mixedVerts.count(k)) anyCorner = true;
    if (!anyCorner) {
      out.tri(P0, P1, P2);
      return;
    }
    // Walk the three directed edges, emitting each tail vertex's inset then any
    // strip feet that fall on that edge — near-tail first, near-head second.
    std::vector<Vector3d> poly;
    auto footOn = [&](int a, int b) -> std::optional<Vector3d> {
      if (!mixedVerts.count(a) || !isSelected(a, b)) return std::nullopt;
      const Vector3d f = stripFoot(a, b, ti);
      if ((f - insetForTri(a, ti)).norm() < 1e-9) return std::nullopt;  // at the mitre
      return f;
    };
    for (int i = 0; i < 3; ++i) {
      const int a = v[i], b = v[(i + 1) % 3];
      poly.push_back(P[i]);
      if (auto fa = footOn(a, b)) poly.push_back(*fa);        // near tail a
      if (auto fb = footOn(b, a)) poly.push_back(*fb);        // near head b
    }
    if (!fillPlanarPolygon(poly)) out.tri(P0, P1, P2);
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
      if (spread > 0.15 * size) return {};
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
    if (std::abs(tube(C0, 0, g0a) - 2 * size) > 0.05 * size) return {};
    if (std::abs(tube(C0, 1, g0b) - 2 * size) > 0.05 * size) return {};

    std::vector<Vector3d> chain{C0};
    Vector3d c = C0, prevT = Vector3d::Zero();
    double prevAng = std::numeric_limits<double>::max();
    const double h = 0.12 * size;
    for (int step = 0; step < 256; ++step) {
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
      for (int it = 0; it < 8; ++it) {  // Newton back onto both tubes
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
    const double arm = 0.42;
    auto build = [&](const std::vector<int>& idx, std::vector<Vector3d>& P,
                     std::vector<Vector3d>& M, std::vector<double>& t, std::vector<Vector3d>& C) {
      const int m = static_cast<int>(idx.size());
      P.resize(m); M.resize(m); t.resize(m); C.resize(m);
      for (int i = 0; i < m; ++i) P[i] = out.V[ring[idx[i]]];
      double acc = 0; t[0] = 0;
      for (int i = 1; i < m; ++i) { acc += (P[i] - P[i - 1]).norm(); t[i] = acc; }
      for (int i = 1; i < m; ++i) t[i] = acc > 1e-12 ? t[i] / acc : static_cast<double>(i) / (m - 1);
      for (int i = 0; i < m; ++i) {
        M[i] = tangentCtrl(P[i], ringNrm[idx[i]], Q, arm);
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
      for (const auto& p : ccC) tight = tight && (p - C0).norm() <= 0.15 * size;
      if (tight) roll = cornerRoll(ring, ringSign, cc, cv, cvC, C0, Q);
    }

    const int K = std::max(2, arcSegs);  // cross-curve samples, matches strip arcSegs

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
    // outer band of each end are
    // clamped flush onto that end's fillet surface (weight smoothstepped to zero across
    // the band) so the patch continues the strip near the seam instead of bulging proud;
    // the boundary layers (s=0, s=1) are left untouched so they stay welded to the ring.
    const double band = 0.35;  // outer fraction of the cross curve kept flush to the strip
    auto crossCurve = [&](int i, int j, bool edge, std::vector<Vector3d>& lay) {
      lay.clear();
      const int steps = edge ? 1 : K;
      for (int l = 0; l <= steps; ++l) {
        const double s = static_cast<double>(l) / steps;
        const double u = 1 - s;
        Vector3d p = u * u * u * ccP[i] + 3 * u * u * s * ccM[i] +
                     3 * u * s * s * cvM[j] + s * s * s * cvP[j];
        if (l > 0 && l < steps) {  // never move the welded boundary layers
          const double ec = std::clamp((band - s) / band, 0.0, 1.0);
          const double ev = std::clamp((band - (1 - s)) / band, 0.0, 1.0);
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
    const int R = static_cast<int>(seq.size());
    std::vector<std::vector<Vector3d>> grid(R);
    for (int r = 0; r < R; ++r)
      crossCurve(seq[r].first, seq[r].second, /*edge=*/r == 0 || r == R - 1, grid[r]);

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
      const double rowMargin = 3, sMargin = 0.15;
      std::vector<double> rw(R);
      for (int r = 0; r < R; ++r) {
        const double d = std::clamp(std::min(r, R - 1 - r) / rowMargin, 0.0, 1.0);
        rw[r] = d * d * (3 - 2 * d);
      }
      std::vector<double> sw(K + 1);
      for (int l = 0; l <= K; ++l) {
        const double s = static_cast<double>(l) / K;
        const double d = std::clamp(std::min(s, 1 - s) / sMargin, 0.0, 1.0);
        sw[l] = d * d * (3 - 2 * d);
      }
      // A row's layer l, for the two chord rows read at the same s as the rest.
      auto at = [&](int r, int l) {
        const std::vector<Vector3d>& g = grid[r];
        if (static_cast<int>(g.size()) == K + 1) return g[l];
        const double s = static_cast<double>(l) / K;
        return Vector3d(g.front() + s * (g.back() - g.front()));
      };
      auto draw = [&](int r, int l, Vector3d p) {
        const Vector3d q = rollProject(roll, p);
        if ((q - p).norm() < 0.75 * size) p += rw[r] * sw[l] * (q - p);
        return p;
      };
      for (int r = 1; r + 1 < R; ++r)
        if (static_cast<int>(grid[r].size()) == K + 1)
          for (int l = 1; l < K; ++l) grid[r][l] = draw(r, l, grid[r][l]);
      // enough rounds for the relaxation to reach across the fan
      for (int pass = 0; pass < 48; ++pass) {
        std::vector<std::vector<Vector3d>> next = grid;
        for (int r = 1; r + 1 < R; ++r) {
          if (static_cast<int>(grid[r].size()) != K + 1) continue;
          for (int l = 1; l < K; ++l) {
            const Vector3d avg =
                0.25 * (at(r - 1, l) + at(r + 1, l) + grid[r][l - 1] + grid[r][l + 1]);
            next[r][l] = draw(r, l, grid[r][l] + 0.85 * (avg - grid[r][l]));
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
      M[i] = B[i] + 0.55 * d.norm() * t;
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
    const double weldTol = 0.06 * size;
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

    // Verify the weld left an edge-manifold patch. A proximity weld can merge two
    // vertices that are not a collapsible neighbour pair, fusing separate columns so
    // an interior edge ends up shared by three or more surviving triangles — the
    // manifold surgery then rejects the whole blend and returns the model unchanged.
    // Count each surviving triangle's undirected edges; if any is used more than
    // twice the weld is unsafe for this ring, so drop it and emit the plain fan.
    // (The saddle is still a valid, if slightly slivered, patch without the weld.)
    std::map<std::pair<int, int>, int> edgeUse;
    bool manifold = true;
    for (const auto& t : F) {
      const int a = find(t[0]), b = find(t[1]), c = find(t[2]);
      if (a == b || b == c || a == c) continue;  // degenerate, dropped by out.tri too
      for (const auto& e : {std::minmax(a, b), std::minmax(b, c), std::minmax(a, c)})
        if (++edgeUse[e] > 2) { manifold = false; break; }
      if (!manifold) break;
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
    auto same = [](const Vector3d& p, const Vector3d& q) { return (p - q).norm() < 1e-6; };
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
    if (selVia.empty()) return;

    // A smooth crease passing through u: exactly two selected edges whose
    // edge-local cross-sections coincide. emitEdge already welds their two strips
    // along that shared cross-section (each shared segment carries one quad from
    // either strip), so u is watertight and a patch would double-cover it. This is
    // the rim/pass-through skip — geometric, so it holds whether or not the wall is
    // split into per-facet surfaces (the surface-pair count no longer decides it).
    if (selVia.size() == 2 &&
        sectionsWeld(edgeCrossSection(u, selVia[0]), edgeCrossSection(u, selVia[1])))
      return;

    const std::vector<std::pair<int, int>> fan = fanAround(u);
    if (fan.empty()) return;
    const int n = static_cast<int>(fan.size());
    int start = -1;
    for (int i = 0; i < n; ++i)
      if (isSelected(u, fan[i].second)) { start = i; break; }
    if (start < 0) return;

    // Walk the fan once, building the ring around u: the sector-local inset point
    // wherever a run of triangles turns, and each selected edge's arc interior
    // where the walk crosses it — edge-local throughout, so the ring shares its
    // vertices with the strips exactly. One ring per vertex covers a genuine
    // junction, a strip end against a kept-sharp boundary, and both at once.
    // Each ring point carries the blend surface's own normal there (ringNrm),
    // parallel to ring: a connector (inset) point takes its face normal; an arc
    // point takes (p - C) off its fillet centre. The mixed-sign saddle reads these
    // to leave every boundary point along the fillet's tangent. pushRing keeps the
    // two arrays in lockstep through the same adjacent-duplicate dedup.
    std::vector<int> ring;
    std::vector<Vector3d> ringNrm;
    std::vector<int> ringSign;    // +1 convex arc, -1 concave arc, 0 connector (inset)
    std::vector<int> cornerPos;  // ring indices of the sector-inset corners (the arc joins)
    auto pushRing = [&](int idx, const Vector3d& nrm, bool corner, int sign) {
      if (!ring.empty() && ring.back() == idx) return;
      if (corner) cornerPos.push_back(static_cast<int>(ring.size()));
      ring.push_back(idx);
      ringNrm.push_back(nrm);
      ringSign.push_back(sign);
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
        const size_t lo = mixedVerts.count(u) ? 0 : 1;
        const size_t hi = mixedVerts.count(u) ? cs.size() : cs.size() - 1;
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
    if (ring.size() >= 2 && ring.front() == ring.back()) { ring.pop_back(); ringNrm.pop_back(); ringSign.pop_back(); }
    while (!cornerPos.empty() && cornerPos.back() >= static_cast<int>(ring.size())) cornerPos.pop_back();

    // Sign of the selected edges here.
    bool anyConcave = false, anyConvex = false;
    for (const int via : selVia) {
      const EdgeKey e{std::min(u, via), std::max(u, via)};
      (concaveOf.at(e) ? anyConcave : anyConvex) = true;
    }
    const bool mixed = anyConcave && anyConvex;
    const bool junction = selVia.size() >= 2;  // (the weld pass-through already returned)

    // Genuine single-signed junction: tessellate the corner-ball sphere as a
    // rounded cap so it rounds a convex vertex off / fills a concave one — the
    // pole is the sphere point toward the original sharp vertex (its outermost
    // point for a convex corner, the deepest valley point for a concave one).
    // Only for a real junction — a strip end gets a flat cap instead.
    if (!isChamfer && junction && !mixed && ring.size() >= 3) {
      if (auto C = cornerBall(u, anyConcave)) {
        // A convex box corner (three equal fillet arcs) tessellates as a subdivided
        // spherical triangle — no central pole, facets flowing with the strips — so
        // it does not read as a beaded knuckle. Concave caps and any ring that is not
        // three equal arcs fall through to the pole-fan below.
        if (!anyConcave && emitCapTri(ring, cornerPos, *C)) return;
        Vector3d poleUnit = (m.pos[u] - *C).normalized();
        if (poleUnit.squaredNorm() < 0.5) {  // u sits on the centre: fall back to the ring
          Vector3d centroid = Vector3d::Zero();
          for (const int i : ring) centroid += out.V[i];
          centroid /= static_cast<double>(ring.size());
          poleUnit = (centroid - *C).normalized();
        }
        emitCap(ring, *C, size, poleUnit);
        return;
      }
    }

    // Mixed-sign junction: a convex edge and a concave edge share u, so no single
    // ball caps it — fill the ring with a curved saddle membrane instead of a flat
    // patch, removing the creased V-notch the ear-clip left where a concave crease
    // died into a face.
    if (!isChamfer && junction && mixed && ring.size() >= 4 &&
        (emitCoonsSaddle(ring, ringNrm, ringSign) || emitSaddle(ring, ringNrm)))
      return;

    // Strip end (a fillet ending against a kept-sharp boundary): close the ring
    // flat by ear-clipping it in its best-fit plane — the perpendicular patch that
    // seals the volume and leaves the kept edges sharp. Falls back to the crude
    // centroid fan if the ring is too tangled to triangulate cleanly (orient()
    // then refuses if that left a hole).
    if (ring.size() >= 4 && ringSaddle(ring)) return;
    out.fan(ring);
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
        const Vector3d T = lineIntersect(b1, dd1, b2, dd2);
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
    for (size_t t = 0; t < m.tris.size(); ++t) {
      // Inset each vertex on the side triangle t sits on — sector-local, so a
      // facet of a split wall sets back along its own sector rather than mitring
      // across a surface it is no longer grouped with. Where the triangle meets a
      // mixed corner it is re-triangulated to carry the pulled-in strip feet.
      emitSurfaceTri(static_cast<int>(t));
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

  const MergedMesh m0 = mergeMesh(target->getManifold().GetMeshGL64());
  const double thresholdDeg = node.min_angle >= 0 ? node.min_angle : kDefaultCreaseThresholdDeg;

  // The selection brush (the union of the node's brush children), as the solid a
  // crease edge is tested against: an edge is blended only where its spine lies
  // inside the brush. Absent when the node has no brushes (the whole model).
  std::optional<BrushVolume> brushVol;
  if (brush && !brush->isEmpty()) brushVol.emplace(brush->getManifold().GetMeshGL64());

  // One blend attempt on a given mesh. The whole build is mesh-dependent, so it
  // is factored out to be runnable twice: once on the subdivided mesh, once on
  // the raw one if subdivision produced a non-manifold result (below).
  enum class Status { Ok, Empty, NoSelection, Holed };
  struct Attempt
  {
    Status status = Status::Empty;
    std::unique_ptr<PolySet> geom;
    std::size_t selected = 0, nConcave = 0, nConvex = 0;
    std::size_t verts = 0, tris = 0;
    int boundary = 0, nonman = 0;
  };
  auto buildOn = [&](const MergedMesh& m, double surfaceThresholdDeg, bool pullIn,
                     double stationFrac) -> Attempt {
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
      if (brushVol && !brushVol->contains(0.5 * (m.pos[key.first] + m.pos[key.second]))) continue;
      b.selected.insert(key);
      b.concaveOf[key] = ce.concave;
      if (ce.concave) ++a.nConcave; else ++a.nConvex;
    }
    a.selected = b.selected.size();
    if (b.selected.empty()) { a.status = Status::NoSelection; return a; }
    b.run();
    a.boundary = b.out.orient();
    a.nonman = b.out.nonManifoldEdges();
    if (b.out.F.empty()) { a.status = Status::Empty; return a; }
    // A hole or a self-overlap both make an invalid solid; refuse either rather
    // than emit it (a false refusal is the safe error, promise 1).
    if (a.boundary > 0 || a.nonman > 0) { a.status = Status::Holed; return a; }
    a.status = Status::Ok;
    a.verts = b.out.V.size();
    a.tris = b.out.F.size();
    a.geom = b.out.build();
    return a;
  };

  // Along-sweep station floor (cap kAlongSweep * size, named at file scope with
  // the angle caps): split long selected creases so a straight fillet holds a
  // constant profile instead of tapering. Where subdivision densifies a
  // degenerate region (two fillets colliding along an exact tangency line) it can
  // turn a marginally-valid over-size case non-manifold; there, fall back to the
  // un-subdivided build, which is never worse than before this floor existed.
  MergedMesh mSub = m0;
  // The edges to densify are exactly the ones buildOn will blend: the eligible
  // set computed on the raw mesh at the same two thresholds, tool-sign filtered.
  // Taking it from selectCreaseEdges (rather than the feature key alone) means a
  // shallow tangent stretch carried into the selection by crease-following — a
  // tee's tangent sides — is given stations too, so a straight fillet there holds
  // its profile instead of tapering to the corner-distorted ends.
  {
    const std::map<EdgeKey, std::vector<int>> adj0 = buildEdgeAdjacency(m0.tris);
    const CreaseSelection sel0 =
      selectCreaseEdges(m0, adj0, thresholdDeg, kDefaultSurfaceThresholdDeg);
    std::set<EdgeKey> toSplit;
    for (const auto& key : sel0.eligible)
      if (sel0.crease.at(key).concave ? node.concave : node.convex) toSplit.insert(key);
    subdivideLongCreaseEdges(mSub, toSplit, kAlongSweep * node.size);
  }
  const bool didSubdivide = mSub.tris.size() != m0.tris.size();

  // Prefer the tight pulled-in seating (round-overs flow into the corner); if it
  // leaves the mesh open on an oblique or crowded corner it cannot weld, fall back
  // to the full-mitre pull-in (the un-blade seat that only ever shrinks the patch),
  // then to the baseline mitre seating — each on the subdivided mesh, then on the
  // raw one if subdivision itself went non-manifold. Each tier is never worse than
  // the next, so a corner that cannot take the tight seat gets the full mitre, and
  // one that cannot take the pull-in at all gets exactly the baseline blend.
  Attempt a = buildOn(mSub, kDefaultSurfaceThresholdDeg, /*pullIn=*/true, kPullStationFrac);
  if (a.status == Status::Holed) a = buildOn(mSub, kDefaultSurfaceThresholdDeg, true, 1.0);
  if (a.status == Status::Holed) a = buildOn(mSub, kDefaultSurfaceThresholdDeg, false, 1.0);
  if (didSubdivide && a.status == Status::Holed)
    a = buildOn(m0, kDefaultSurfaceThresholdDeg, false, 1.0);

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
    case Status::Ok:
      break;
  }

  LOG(message_group::Echo, node.modinst->location(), "",
      "%1$s: %2$s %3$g blended %4$d edge(s) (%5$d concave, %6$d convex); %7$d verts, %8$d tris",
      node.name(), sizeName, node.size, static_cast<int>(a.selected),
      static_cast<int>(a.nConcave), static_cast<int>(a.nConvex), static_cast<int>(a.verts),
      static_cast<int>(a.tris));
  return std::move(a.geom);
}
