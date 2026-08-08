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
#include "geometry/fillet/FilletBuilder_internal.h"
#include "geometry/linalg.h"
#include "geometry/manifold/ManifoldGeometry.h"
#include "utils/printutils.h"

using namespace fillet::detail;

namespace {

// ---------------------------------------------------------------------------
// Tunable angle thresholds for the blend's crease walk. Two companions live
// with the classification core in the internal header: the feature/crease
// threshold (min_angle, defaulting to kDefaultCreaseThresholdDeg) and the
// smooth-surface grouping threshold (kDefaultSurfaceThresholdDeg 10 deg). The
// two below are specific to this file and are named here, in one place, rather
// than left as a literal buried in each function — the "same 40 deg" the
// selection walk and the pass-through seating both rely on is then tuned once.
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

  // The blend cross-section at vertex u for one selected edge, its two sides given
  // by the edge's incident triangles t0/t1 — LOCAL, so it builds a proper
  // two-sided arc even where t0/t1's walls are split into per-facet surfaces. The
  // seat normal comes from each triangle's sector (averaged), so adjacent edges on
  // a smooth crease build the identical cross-section at u and their strips weld.
  std::vector<Vector3d> crossSectionEdge(int u, int t0, int t1, bool concave) const
  {
    const Vector3d Ta = insetForTri(u, t0);
    const Vector3d Tb = insetForTri(u, t1);
    if (isChamfer) return {Ta, Tb};
    const auto sa = sectorOf(u, t0);
    const auto sb = sectorOf(u, t1);
    if (!sa || !sb) return {Ta, Tb};
    Vector3d nA = sa->navg, nB = sb->navg;
    // Same override as the inset: on a shared pass-through the two strips must use
    // one seat normal per side, so their arc points coincide and weld.
    if (const auto it = normalOverride.find({u, t0}); it != normalOverride.end()) nA = it->second;
    if (const auto it = normalOverride.find({u, t1}); it != normalOverride.end()) nB = it->second;

    const double r = size;
    // Fillet centre one radius off side A along its sector normal — the plain
    // rolling-ball construction, so a planar crease builds the same arc a
    // surface-based inset would. To stay independent of which incident triangle
    // adj happened to list first (so two edges continuing one smooth crease
    // weld), side A is chosen canonically as
    // the sector with the numerically smaller averaged normal.
    const bool aFirst = std::make_tuple(nA.x(), nA.y(), nA.z()) <=
                        std::make_tuple(nB.x(), nB.y(), nB.z());
    const Vector3d nCen = aFirst ? nA : nB;
    const Vector3d Tcen = aFirst ? Ta : Tb;
    const double sgn = concave ? r : -r;
    const Vector3d C = Tcen + sgn * nCen;
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
    std::vector<int> ring;
    for (int off = 0; off < n; ++off) {
      const int i = (start + off) % n;
      const int tri = fan[i].first;
      const int via = fan[i].second;  // edge (u,via) leaving this triangle
      const int ip = out.add(insetForTri(u, tri));
      if (ring.empty() || ring.back() != ip) ring.push_back(ip);
      if (isSelected(u, via)) {
        const EdgeKey e{std::min(u, via), std::max(u, via)};
        const auto& ts = adj.at(e);
        std::vector<Vector3d> cs = crossSectionEdge(u, ts[0], ts[1], concaveOf.at(e));
        if (ts[0] != tri) std::reverse(cs.begin(), cs.end());  // start on this run's side
        for (size_t j = 1; j + 1 < cs.size(); ++j) ring.push_back(out.add(cs[j]));
      }
    }
    if (ring.size() >= 2 && ring.front() == ring.back()) ring.pop_back();

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

    // Strip end (a fillet ending against a kept-sharp boundary) and mixed-sign
    // junction alike: close the ring flat by ear-clipping it in its best-fit
    // plane — the perpendicular patch that seals the volume and leaves the kept
    // edges sharp. Falls back to the crude centroid fan if the ring is too tangled
    // to triangulate cleanly (orient() then refuses if that left a hole).
    if (ring.size() >= 4 && ringSaddle(ring)) return;
    out.fan(ring);
  }

  // Triangulate a (non-planar) ring directly, no central vertex, by ear-clipping
  // in its best-fit plane. Returns false — leaving the caller to fall back — if
  // the projection is degenerate or no ear can be found.
  bool ringSaddle(const std::vector<int>& ring)
  {
    const int n = static_cast<int>(ring.size());
    Vector3d c = Vector3d::Zero();
    for (const int i : ring) c += out.V[i];
    c /= n;
    // best-fit normal via Newell's method
    Vector3d nrm = Vector3d::Zero();
    for (int i = 0; i < n; ++i) {
      const Vector3d& a = out.V[ring[i]];
      const Vector3d& b = out.V[ring[(i + 1) % n]];
      nrm += (a - c).cross(b - c);
    }
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
    // signed area to fix winding
    double area = 0;
    for (int i = 0; i < n; ++i) area += p[i].x() * p[(i + 1) % n].y() - p[(i + 1) % n].x() * p[i].y();
    std::vector<int> idx(n);
    for (int i = 0; i < n; ++i) idx[i] = (area < 0) ? (n - 1 - i) : i;
    auto cross2 = [](const Vector2d& a, const Vector2d& b, const Vector2d& cc) {
      return (b.x() - a.x()) * (cc.y() - a.y()) - (b.y() - a.y()) * (cc.x() - a.x());
    };
    std::vector<int> poly = idx;
    std::vector<std::array<int, 3>> tris;
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
        tris.push_back({ring[ia], ring[ib], ring[ic]});
        poly.erase(poly.begin() + i);
        clipped = true;
        break;
      }
      if (!clipped) return false;
    }
    if (poly.size() == 3) tris.push_back({ring[poly[0]], ring[poly[1]], ring[poly[2]]});
    for (const auto& t : tris) out.tri(t[0], t[1], t[2]);
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
    prepareShared();
    for (size_t t = 0; t < m.tris.size(); ++t) {
      const int ti = static_cast<int>(t);
      const auto& v = m.tris[t].v;
      // Inset each vertex on the side triangle t sits on — sector-local, so a
      // facet of a split wall sets back along its own sector rather than mitring
      // across a surface it is no longer grouped with.
      out.tri(insetForTri(v[0], ti), insetForTri(v[1], ti), insetForTri(v[2], ti));
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
  auto buildOn = [&](const MergedMesh& m, double surfaceThresholdDeg) -> Attempt {
    const std::map<EdgeKey, std::vector<int>> adj = buildEdgeAdjacency(m.tris);
    // Group surfaces by near-tangency, not by the feature threshold: a sub-crease
    // seam that is not near-tangent (a tee's tangent gap) must stay a surface
    // boundary so the two walls keep distinct ids and the junction cross-section
    // does not degenerate.
    const std::vector<int> surfaceOf = smoothSurfaces(m, adj, surfaceThresholdDeg);
    Blender b{m,            adj,       surfaceOf, node.size, isChamfer,
              thresholdDeg, node.discretizer};
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

  // Along-sweep station floor: split long selected creases so a straight fillet
  // holds a constant profile instead of tapering (see along-sweep-stations.md).
  // cap = k*size, k = 4 — a constant for now, an along-sweep counterpart to the
  // arc discretizer to be exposed later. Where subdivision densifies a
  // degenerate region (two fillets colliding along an exact tangency line) it can
  // turn a marginally-valid over-size case non-manifold; there, fall back to the
  // un-subdivided build, which is never worse than before this floor existed.
  constexpr double kAlongSweep = 4.0;
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

  Attempt a = buildOn(mSub, kDefaultSurfaceThresholdDeg);
  if (didSubdivide && a.status == Status::Holed) a = buildOn(m0, kDefaultSurfaceThresholdDeg);

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
