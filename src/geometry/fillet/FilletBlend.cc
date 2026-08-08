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
#include "geometry/fillet/FilletBuilder_internal.h"
#include "geometry/linalg.h"
#include "geometry/manifold/ManifoldGeometry.h"
#include "utils/printutils.h"

using namespace fillet::detail;

namespace {

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

// The second density floor (`along-sweep-stations.md`): before the blend runs,
// give every long selected crease enough stations that a straight fillet holds a
// constant cross-section instead of tapering between its two corner-distorted
// ends. Split each selected feature edge longer than `cap` into ceil(len/cap)
// equal pieces. Splitting only ever shortens; it never turns a seam into a
// feature, so one pass taken from the original mesh is complete.
void subdivideLongCreaseEdges(MergedMesh& m, double thresholdDeg, bool wantConvex,
                              bool wantConcave, double cap)
{
  if (!(cap > 0)) return;
  const std::map<EdgeKey, std::vector<int>> adj = buildEdgeAdjacency(m.tris);
  struct Split { int a, b, parts; };
  std::vector<Split> todo;
  for (const auto& [key, ts] : adj) {
    if (ts.size() != 2) continue;
    const EdgeClass ec = classifyEdge(m, key, m.tris[ts[0]], m.tris[ts[1]]);
    if (!isFeatureAngle(ec.dihedralDeg, thresholdDeg)) continue;
    if (!(ec.concave ? wantConcave : wantConvex)) continue;
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

  // The ordered surface pair of a selected edge (smaller surface id first) and
  // its sign, so both ends of a strip and both incident strips at a vertex agree
  // on which tangent point is which.
  std::pair<int, int> pairOf(const EdgeKey& e) const
  {
    const auto& ts = adj.at(e);
    return {std::min(surfaceOf[ts[0]], surfaceOf[ts[1]]),
            std::max(surfaceOf[ts[0]], surfaceOf[ts[1]])};
  }

  // The cross-section of the blend at vertex u for one crease side-pair
  // {SA, SB}: from the tangent point on SA to the tangent point on SB — a flat
  // segment for a chamfer, a tessellated arc for a fillet. The wall normals are
  // AVERAGED over the selected edges of this pair meeting at u, so two strips
  // continuing a smooth crease (a rim) share this cross-section exactly and need
  // no corner patch between them; only a genuine junction (a second pair) does.
  std::vector<Vector3d> crossSectionAt(int u, int SA, int SB) const
  {
    const Vector3d Ta = insetPoint(u, SA);
    const Vector3d Tb = insetPoint(u, SB);
    if (isChamfer) return {Ta, Tb};

    Vector3d nA = Vector3d::Zero();
    bool concave = false;
    int cnt = 0;
    for (const auto& [key, ts] : adj) {
      if (key.first != u && key.second != u) continue;
      if (!selected.count(key)) continue;
      const int a = surfaceOf[ts[0]], c = surfaceOf[ts[1]];
      if (std::min(a, c) != SA || std::max(a, c) != SB) continue;
      nA += m.tris[a == SA ? ts[0] : ts[1]].normal;
      concave = concaveOf.at(key);
      ++cnt;
    }
    if (cnt == 0) return {Ta, Tb};
    nA.normalize();

    const double r = size;
    const Vector3d C = concave ? Vector3d(Ta + r * nA) : Vector3d(Ta - r * nA);
    Vector3d ra = Ta - C, rb = Tb - C;
    const double la = ra.norm(), lb = rb.norm();
    if (la < 1e-9 || lb < 1e-9) return {Ta, Tb};
    ra /= la;
    rb /= lb;
    double ang = std::acos(std::clamp(ra.dot(rb), -1.0, 1.0));
    if (ang < 1e-6) return {Ta, Tb};
    const int segs = arcSegs;
    Vector3d axis = ra.cross(rb);
    if (axis.norm() < 1e-12) return {Ta, Tb};
    axis.normalize();
    std::vector<Vector3d> pts;
    pts.reserve(segs + 1);
    for (int j = 0; j <= segs; ++j) {
      const double a = ang * j / segs;
      // Rodrigues rotation of ra about axis by a, radius interpolated la->lb.
      const Vector3d rot = ra * std::cos(a) + axis.cross(ra) * std::sin(a);
      const double rad = la + (lb - la) * j / segs;
      pts.push_back(C + rad * rot);
    }
    return pts;
  }

  // The strip along one selected edge: the cross-sections at its two ends,
  // stitched into quads.
  void emitEdge(const EdgeKey& e)
  {
    const auto [SA, SB] = pairOf(e);
    const std::vector<Vector3d> cu = crossSectionAt(e.first, SA, SB);
    const std::vector<Vector3d> cv = crossSectionAt(e.second, SA, SB);
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

  void emitCorner(int u)
  {
    // A corner patch is needed only at a genuine junction: a vertex where the
    // selected edges carry more than one crease side-pair. Where they all share
    // one pair (a rim, or a smooth crease passing through) the adjacent strips
    // already share the averaged cross-section at u and close without a patch.
    std::set<std::pair<int, int>> pairs;
    for (const auto& [key, ts] : adj) {
      if ((key.first == u || key.second == u) && selected.count(key)) pairs.insert(pairOf(key));
    }
    if (pairs.size() < 2) return;

    const std::vector<std::pair<int, int>> fan = fanAround(u);
    if (fan.empty()) return;
    // Build the ring around u: inset(u,S) for each surface run, plus each
    // selected edge's arc interior points, in rotational order.
    const int n = static_cast<int>(fan.size());
    int start = -1;
    for (int i = 0; i < n; ++i)
      if (isSelected(u, fan[i].second)) {
        start = i;
        break;
      }
    if (start < 0) return;

    std::vector<int> ring;
    for (int off = 0; off < n; ++off) {
      const int i = (start + off) % n;
      const int S = surfaceOf[fan[i].first];
      const int via = fan[i].second;  // edge (u,via) leaving this triangle
      const int ip = out.add(insetPoint(u, S));
      if (ring.empty() || ring.back() != ip) ring.push_back(ip);
      if (isSelected(u, via)) {
        const EdgeKey e{std::min(u, via), std::max(u, via)};
        const auto [SA, SB] = pairOf(e);
        std::vector<Vector3d> cs = crossSectionAt(u, SA, SB);
        if (S != SA) std::reverse(cs.begin(), cs.end());  // start on this run's surface
        for (size_t j = 1; j + 1 < cs.size(); ++j) ring.push_back(out.add(cs[j]));
      }
    }

    // Sign of this junction: single-signed if every selected edge here agrees.
    bool anyConcave = false, anyConvex = false;
    for (const auto& [key, ts] : adj)
      if ((key.first == u || key.second == u) && selected.count(key))
        (concaveOf.at(key) ? anyConcave : anyConvex) = true;
    const bool mixed = anyConcave && anyConvex;

    // Single-signed corner: seat the fan apex on the ball so it rounds a
    // convex vertex off / fills a concave one, instead of the crude centroid fan
    // that sinks inside the sphere. The ring vertices are shared with the strips
    // and must not move; only the new apex is placed, on the sphere above the
    // ring. Chamfer corners are already flat and keep the plain fan. Mixed-sign
    // vertices are the deferred saddle and stay a crude fan for now.
    if (!isChamfer && !mixed && ring.size() >= 3) {
      if (auto C = cornerBall(u, anyConcave)) {
        Vector3d centroid = Vector3d::Zero();
        for (const int i : ring) centroid += out.V[i];
        centroid /= static_cast<double>(ring.size());
        const Vector3d dir = (centroid - *C).normalized();
        const int apex = out.add(*C + size * dir);
        const int n2 = static_cast<int>(ring.size());
        for (int i = 0; i < n2; ++i) out.tri(apex, ring[i], ring[(i + 1) % n2]);
        return;
      }
    }

    // Mixed-sign vertex: a convex and a concave edge meet, so no single
    // ball fits — the patch must be a saddle. Rather than fan from a central
    // apex (which pins a flat point in the middle and reads as a pinch),
    // triangulate the ring itself so the patch passes only through the tangent
    // and arc points, which already sit on the two opposite-curvature sides — a
    // bilinear saddle for a simple ring. Falls back to the crude centroid fan if
    // the ring is too tangled to triangulate cleanly.
    if (!isChamfer && mixed && ring.size() >= 4 && ringSaddle(ring)) return;
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

  // Re-emit every surface triangle with its boundary vertices set back to their
  // inset positions (interior vertices are unchanged), then the edge strips and
  // the corner patches.
  void run()
  {
    for (size_t t = 0; t < m.tris.size(); ++t) {
      const int S = surfaceOf[t];
      const auto& v = m.tris[t].v;
      out.tri(insetPoint(v[0], S), insetPoint(v[1], S), insetPoint(v[2], S));
    }
    for (const auto& e : selected) emitEdge(e);
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

  // One blend attempt on a given mesh. The whole build is mesh-dependent, so it
  // is factored out to be runnable twice: once on the subdivided mesh, once on
  // the raw one if subdivision produced a non-manifold result (below).
  enum class Status { Ok, Empty, NoSelection, Partial, Holed };
  struct Attempt
  {
    Status status = Status::Empty;
    std::unique_ptr<PolySet> geom;
    std::size_t selected = 0, nConcave = 0, nConvex = 0, features = 0;
    std::size_t verts = 0, tris = 0;
    int boundary = 0, nonman = 0;
  };
  auto buildOn = [&](const MergedMesh& m) -> Attempt {
    const std::map<EdgeKey, std::vector<int>> adj = buildEdgeAdjacency(m.tris);
    const std::vector<int> surfaceOf = smoothSurfaces(m, adj, thresholdDeg);
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
    for (const auto& [key, ts] : adj) {
      if (ts.size() != 2) continue;
      const EdgeClass ec = classifyEdge(m, key, m.tris[ts[0]], m.tris[ts[1]]);
      if (!isFeatureAngle(ec.dihedralDeg, thresholdDeg)) continue;
      ++a.features;
      b.feature.insert(key);
      const bool want = ec.concave ? node.concave : node.convex;
      if (!want) continue;
      b.selected.insert(key);
      b.concaveOf[key] = ec.concave;
      if (ec.concave) ++a.nConcave; else ++a.nConvex;
    }
    a.selected = b.selected.size();
    if (b.selected.empty()) { a.status = Status::NoSelection; return a; }
    // Partial selection — a brush, or a one-sided convex/concave filter on a
    // model that has both signs — leaves some feature edges as kept-sharp surface
    // boundaries. Closing that manifold needs per-vertex splitting where a kept
    // edge borders a blended surface (a full topological bevel); the whole-model
    // selection this file builds does not do that yet. Until it does, refuse to
    // emit a torn mesh: return the model unchanged, loudly — a false refusal is
    // the safe error, a silent drop is not. The kept-edge groundwork already
    // slides the inset along kept edges, so full selection stays exact.
    if ((brush && !brush->isEmpty()) || b.selected.size() != a.features) {
      a.status = Status::Partial;
      return a;
    }
    b.run();
    a.boundary = b.out.orient();
    a.nonman = b.out.nonManifoldEdges();
    if (b.out.F.empty()) { a.status = Status::Empty; return a; }
    if (a.boundary > 0) { a.status = Status::Holed; return a; }
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
  subdivideLongCreaseEdges(mSub, thresholdDeg, node.convex, node.concave, kAlongSweep * node.size);
  const bool didSubdivide = mSub.tris.size() != m0.tris.size();

  Attempt a = buildOn(mSub);
  if (didSubdivide && (a.status == Status::Holed || (a.status == Status::Ok && a.nonman > 0)))
    a = buildOn(m0);

  switch (a.status) {
    case Status::Empty:
      return target;
    case Status::NoSelection:
      LOG(message_group::Warning, node.modinst->location(), "",
          "%1$s: no selected edge turns more than %2$.1f deg; the model is returned unchanged",
          node.name(), thresholdDeg);
      return target;
    case Status::Partial:
      LOG(message_group::Warning, node.modinst->location(), "",
          "%1$s: partial selection (a brush, or one-sided convex/concave on a two-sign model) is "
          "not built yet; the model is returned unchanged [%2$d of %3$d feature edges selected]",
          node.name(), static_cast<int>(a.selected), static_cast<int>(a.features));
      return target;
    case Status::Holed:
      // The surgery left a hole — invalid. Say so and hand back the model
      // unchanged rather than a torn solid: a false refusal is the safe error.
      LOG(message_group::Warning, node.modinst->location(), "",
          "%1$s: the blend left %2$d open edge(s) (unclosed junction); the model is returned "
          "unchanged",
          node.name(), a.boundary);
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
