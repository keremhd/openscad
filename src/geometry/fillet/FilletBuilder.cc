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

#include "geometry/fillet/FilletBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <functional>
#include <locale>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
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

namespace fillet::detail {

MergedMesh mergeMesh(const manifold::MeshGL64& mesh)
{
  const size_t numProp = mesh.numProp;
  const size_t numRawVert = numProp ? mesh.vertProperties.size() / numProp : 0;
  const size_t numTri = mesh.triVerts.size() / 3;

  MergedMesh out;
  out.numRawVert = numRawVert;

  // Merge MeshGL vertices by exact position. A single spatial vertex is emitted
  // once per run it touches (runs differ by surface id), so the raw indices do
  // not give topological adjacency until coincident positions are unified.
  std::map<std::array<double, 3>, int> canonOf;
  std::vector<int> canon(numRawVert);
  for (size_t i = 0; i < numRawVert; ++i) {
    const double x = mesh.vertProperties[i * numProp + 0];
    const double y = mesh.vertProperties[i * numProp + 1];
    const double z = mesh.vertProperties[i * numProp + 2];
    const std::array<double, 3> key{x, y, z};
    auto [it, inserted] = canonOf.try_emplace(key, static_cast<int>(out.pos.size()));
    if (inserted) out.pos.emplace_back(x, y, z);
    canon[i] = it->second;
  }

  // Build triangles with merged indices, outward normals, and per-triangle
  // source id (walk the runs exactly as ManifoldGeometry::toPolySet does).
  out.tris.reserve(numTri);
  size_t run = 0;
  for (size_t t = 0; t < numTri; ++t) {
    const size_t base = t * 3;
    while (run + 1 < mesh.runIndex.size() && base >= mesh.runIndex[run + 1]) ++run;
    const uint32_t id = run < mesh.runOriginalID.size() ? mesh.runOriginalID[run] : 0;
    out.distinctIDs.insert(id);

    const int a = canon[mesh.triVerts[base + 0]];
    const int b = canon[mesh.triVerts[base + 1]];
    const int c = canon[mesh.triVerts[base + 2]];
    Vector3d n = (out.pos[b] - out.pos[a]).cross(out.pos[c] - out.pos[a]);
    const double len = n.norm();
    if (len > 0) n /= len;
    out.tris.push_back({{a, b, c}, n, id});
  }

  return out;
}

std::map<EdgeKey, std::vector<int>> buildEdgeAdjacency(const std::vector<Tri>& tris)
{
  std::map<EdgeKey, std::vector<int>> edgeTris;
  for (size_t t = 0; t < tris.size(); ++t) {
    const auto& tv = tris[t].v;
    for (int e = 0; e < 3; ++e) {
      const int u = tv[e], w = tv[(e + 1) % 3];
      edgeTris[{std::min(u, w), std::max(u, w)}].push_back(static_cast<int>(t));
    }
  }
  return edgeTris;
}

EdgeClass classifyEdge(const MergedMesh& m, const EdgeKey& key, const Tri& A, const Tri& B)
{
  double d = A.normal.dot(B.normal);
  d = std::clamp(d, -1.0, 1.0);
  const double phi = std::acos(d) * 180.0 / M_PI;

  // dot(nA, nB) alone cannot tell an inner corner from an outer one (both give
  // the same angle). Ask whether A's far corner pokes in front of B's plane.
  int aFar = A.v[0];
  for (const int k : A.v)
    if (k != key.first && k != key.second) {
      aFar = k;
      break;
    }
  const bool concave = B.normal.dot(m.pos[aFar] - m.pos[key.first]) > 0;

  return {phi, concave};
}

ClassCounts classifyEdges(const MergedMesh& m,
                          const std::map<EdgeKey, std::vector<int>>& adj,
                          double thresholdDeg, bool useProvenance)
{
  ClassCounts c;
  for (const auto& [key, ts] : adj) {
    if (ts.size() != 2) {
      ++c.nonManifold;
      continue;
    }
    ++c.twoFace;
    const Tri& A = m.tris[ts[0]];
    const Tri& B = m.tris[ts[1]];

    const EdgeClass ec = classifyEdge(m, key, A, B);

    // The angle threshold is the primary filter: keep only creases sharper than
    // any seam the tessellation can produce. Provenance is reported alongside
    // rather than used to override — same-id yet sharp edges are real on hard-
    // edged primitives (a cube's own corners), so it must not silently drop them.
    if (!isFeatureAngle(ec.dihedralDeg, thresholdDeg)) continue;

    ++c.feature;
    if (ec.concave) ++c.featureConcave; else ++c.featureConvex;
    if (useProvenance && A.originalID == B.originalID) ++c.featureSameSurface;
  }
  return c;
}

namespace {

// Marker thickness scaled to the model so it reads at any size; the bounding-box
// diagonal is a stable proxy for overall extent.
double markerHalf(const MergedMesh& m)
{
  Vector3d lo = m.pos[0], hi = m.pos[0];
  for (const auto& p : m.pos) {
    lo = lo.cwiseMin(p);
    hi = hi.cwiseMax(p);
  }
  return 0.5 * std::max((hi - lo).norm() * 0.012, 1e-6);
}

// A thin square-section box straddling the segment p0->p1, all faces one color.
void addBoxMarker(PolySetBuilder& builder, const Vector3d& p0, const Vector3d& p1, double half,
                  const Color4f& color)
{
  Vector3d dir = p1 - p0;
  const double len = dir.norm();
  if (len < 1e-9) return;
  dir /= len;

  const Vector3d ref = std::abs(dir.x()) < 0.9 ? Vector3d::UnitX() : Vector3d::UnitY();
  const Vector3d u = dir.cross(ref).normalized();
  const Vector3d w = dir.cross(u);

  const Vector3d off[4] = {-half * u - half * w, half * u - half * w, half * u + half * w,
                           -half * u + half * w};
  Vector3d a[4], b[4];
  for (int i = 0; i < 4; ++i) {
    a[i] = p0 + off[i];
    b[i] = p1 + off[i];
  }

  auto quad = [&](const Vector3d& q0, const Vector3d& q1, const Vector3d& q2, const Vector3d& q3) {
    builder.beginPolygon(4);
    builder.addVertex(q0);
    builder.addVertex(q1);
    builder.addVertex(q2);
    builder.addVertex(q3);
    builder.endPolygon(color);
  };

  quad(a[0], a[3], a[2], a[1]);  // cap at p0
  quad(b[0], b[1], b[2], b[3]);  // cap at p1
  for (int i = 0; i < 4; ++i) {
    const int j = (i + 1) % 4;
    quad(a[i], a[j], b[j], b[i]);  // four sides
  }
}

// A small axis-aligned cube centered at p, marking a point of interest.
void addCubeMarker(PolySetBuilder& builder, const Vector3d& p, double half, const Color4f& color)
{
  const Vector3d c[8] = {
    p + Vector3d(-half, -half, -half), p + Vector3d(half, -half, -half),
    p + Vector3d(half, half, -half),   p + Vector3d(-half, half, -half),
    p + Vector3d(-half, -half, half),  p + Vector3d(half, -half, half),
    p + Vector3d(half, half, half),    p + Vector3d(-half, half, half),
  };
  auto quad = [&](int i0, int i1, int i2, int i3) {
    builder.beginPolygon(4);
    builder.addVertex(c[i0]);
    builder.addVertex(c[i1]);
    builder.addVertex(c[i2]);
    builder.addVertex(c[i3]);
    builder.endPolygon(color);
  };
  quad(0, 3, 2, 1);  // -z
  quad(4, 5, 6, 7);  // +z
  quad(0, 1, 5, 4);  // -y
  quad(2, 3, 7, 6);  // +y
  quad(1, 2, 6, 5);  // +x
  quad(0, 4, 7, 3);  // -x
}

}  // namespace

std::vector<EdgeKey> selectedEdges(const MergedMesh& m,
                                   const std::map<EdgeKey, std::vector<int>>& adj,
                                   double thresholdDeg, bool wantConcave)
{
  std::vector<EdgeKey> out;
  for (const auto& [key, ts] : adj) {
    if (ts.size() != 2) continue;
    const EdgeClass ec = classifyEdge(m, key, m.tris[ts[0]], m.tris[ts[1]]);
    if (!isFeatureAngle(ec.dihedralDeg, thresholdDeg)) continue;
    if (ec.concave != wantConcave) continue;
    out.push_back(key);
  }
  return out;
}

std::vector<Chain> buildChains(const MergedMesh& m, const std::vector<EdgeKey>& edges)
{
  auto edgeKey = [](int a, int b) { return EdgeKey{std::min(a, b), std::max(a, b)}; };

  std::map<int, std::vector<int>> nbr;
  std::set<EdgeKey> remaining;
  for (const auto& e : edges) {
    nbr[e.first].push_back(e.second);
    nbr[e.second].push_back(e.first);
    remaining.insert(edgeKey(e.first, e.second));
  }

  std::vector<Chain> chains;

  // Open chains: start where the crease terminates or branches (degree != 2) and
  // run along interior degree-2 stations to the next such vertex.
  for (const auto& [v, ns] : nbr) {
    if (ns.size() == 2) continue;
    for (const int w : ns) {
      if (!remaining.erase(edgeKey(v, w))) continue;
      Chain chain;
      chain.verts = {v, w};
      int prev = v, cur = w;
      while (nbr[cur].size() == 2) {
        const int nx = nbr[cur][0] == prev ? nbr[cur][1] : nbr[cur][0];
        if (!remaining.erase(edgeKey(cur, nx))) break;
        chain.verts.push_back(nx);
        prev = cur;
        cur = nx;
      }
      chains.push_back(std::move(chain));
    }
  }

  // Whatever remains is 2-regular: closed rings (the hole-mouth case).
  while (!remaining.empty()) {
    const EdgeKey start = *remaining.begin();
    remaining.erase(remaining.begin());
    Chain chain;
    chain.closed = true;
    chain.verts = {start.first, start.second};
    int prev = start.first, cur = start.second;
    while (true) {
      const int nx = nbr[cur][0] == prev ? nbr[cur][1] : nbr[cur][0];
      if (!remaining.erase(edgeKey(cur, nx))) break;
      if (nx == chain.verts.front()) break;  // ring closed; closing edge consumed
      chain.verts.push_back(nx);
      prev = cur;
      cur = nx;
    }
    chains.push_back(std::move(chain));
  }

  // Canonical ordering, so downstream index references stay put when a parameter
  // nudge leaves the mesh unchanged: orient each chain from its lexicographically
  // smallest vertex, then sort chains by that vertex.
  auto less = [&](int i, int j) {
    const Vector3d& a = m.pos[i];
    const Vector3d& b = m.pos[j];
    if (a.x() != b.x()) return a.x() < b.x();
    if (a.y() != b.y()) return a.y() < b.y();
    return a.z() < b.z();
  };
  for (Chain& chain : chains) {
    auto& vs = chain.verts;
    if (chain.closed) {
      const auto mi = std::min_element(vs.begin(), vs.end(), less);
      std::rotate(vs.begin(), mi, vs.end());
      if (vs.size() > 2 && less(vs.back(), vs[1])) std::reverse(vs.begin() + 1, vs.end());
    } else if (less(vs.back(), vs.front())) {
      std::reverse(vs.begin(), vs.end());
    }
  }
  std::sort(chains.begin(), chains.end(),
            [&](const Chain& a, const Chain& b) { return less(a.verts.front(), b.verts.front()); });

  // The crease as the mesh has it. `at` and `pts` are left empty, which is the
  // identity: one station per crease vertex, standing exactly on it.
  for (Chain& chain : chains) chain.raw = chain.verts;

  return chains;
}

void resampleChains(const MergedMesh& m, std::vector<Chain>& chains, double fraction)
{
  if (!(fraction > 0.0)) return;

  for (Chain& chain : chains) {
    const std::vector<int>& run = chain.rawRun();
    const int n = static_cast<int>(run.size());
    // Two stations on an open chain are both its ends, and three on a ring are
    // the least that still bounds an area; there is nothing to redivide.
    if (n < (chain.closed ? 4 : 3)) continue;

    const int segments = chain.closed ? n : n - 1;
    std::vector<double> len(segments);
    for (int i = 0; i < segments; ++i)
      len[i] = (m.pos[run[(i + 1) % n]] - m.pos[run[i]]).norm();

    std::vector<double> sorted = len;
    std::nth_element(sorted.begin(), sorted.begin() + segments / 2, sorted.end());
    const double median = sorted[segments / 2];
    if (!(median > 0.0)) continue;

    // Nothing is a sliver: leave the chain exactly as it was, so a crease that
    // was already regular is not perturbed by a hair.
    const double floorLen = fraction * median;
    if (std::all_of(len.begin(), len.end(), [&](double l) { return l >= floorLen; })) continue;

    std::vector<double> arc(segments + 1, 0.0);
    for (int i = 0; i < segments; ++i) arc[i + 1] = arc[i] + len[i];
    const double total = arc[segments];
    if (!(total > 0.0)) continue;

    // As many stations as the crease has segments, at equal arc length. The
    // count is what it was, so nothing about how closely the bead follows the
    // wall changes; only the spacing does, from a spread of a thousand to one
    // down to exactly even.
    const int count = segments;
    const double step = total / count;

    // Arc length back to the crease's own parameter. The search is over the
    // cumulative arc, so a sliver contributes an interval of zero width and is
    // simply passed over rather than landed on.
    auto paramAt = [&](double s) {
      const int i = std::clamp(
        static_cast<int>(std::upper_bound(arc.begin(), arc.end(), s) - arc.begin()) - 1, 0,
        segments - 1);
      return static_cast<double>(i) + (len[i] > 0.0 ? (s - arc[i]) / len[i] : 0.0);
    };

    std::vector<double> at;
    std::vector<Vector3d> pts;
    std::vector<int> verts;
    const int stations = chain.closed ? count : count + 1;
    at.reserve(stations);
    pts.reserve(stations);
    verts.reserve(stations);
    for (int k = 0; k < stations; ++k) {
      // The ends are the crease's ends and are taken from the mesh, not solved
      // for: k = 0 is vertex 0 (a ring's canonical start as much as an open
      // chain's first vertex), and an open chain's last station is its last
      // vertex. Everything between is placed by arc length, and lands on a
      // vertex exactly when the arithmetic puts it there.
      double p;
      if (k == 0)
        p = 0.0;
      else if (!chain.closed && k == count)
        p = static_cast<double>(segments);
      else
        p = paramAt(static_cast<double>(k) * step);

      const int j = static_cast<int>(p);
      const double f = p - static_cast<double>(j);
      at.push_back(p);
      if (f <= 0.0) {
        verts.push_back(run[j % n]);
        pts.push_back(m.pos[run[j % n]]);
      } else {
        const Vector3d& a = m.pos[run[j]];
        const Vector3d& b = m.pos[run[(j + 1) % n]];
        verts.push_back(-1);
        pts.push_back(a + f * (b - a));
      }
    }

    if (at.size() < static_cast<size_t>(chain.closed ? 3 : 2)) continue;
    chain.at = std::move(at);
    chain.pts = std::move(pts);
    chain.verts = std::move(verts);
  }
}

std::vector<SpineInterval> chainSelection(const MergedMesh& m, const Chain& chain,
                                          const BrushVolume& brush, double debounce)
{
  const size_t n = chain.verts.size();
  const size_t segments = n < 2 ? 0 : (chain.closed ? n : n - 1);
  if (segments == 0 || brush.empty()) return {};

  struct Event
  {
    double p;
    bool entering;
  };
  std::vector<Event> events;
  std::vector<double> segmentLength(segments);
  for (size_t i = 0; i < segments; ++i) {
    const Vector3d& a = chain.point(m.pos, static_cast<int>(i));
    const Vector3d& b = chain.point(m.pos, static_cast<int>((i + 1) % n));
    segmentLength[i] = (b - a).norm();
    for (const auto& c : brush.segmentCrossings(a, b))
      events.push_back({static_cast<double>(i) + c.t, c.entering});
  }
  std::sort(events.begin(), events.end(),
            [](const Event& x, const Event& y) { return x.p < y.p; });

  // A crossing says which way it goes, so the state before the first one is
  // read off it directly. Only a chain that crosses nothing needs the brush
  // asked about a point, and then one point settles the whole chain.
  bool inside = events.empty() ? brush.contains(chain.point(m.pos, 0)) : !events.front().entering;

  std::vector<SpineInterval> keep;
  double open = inside ? 0.0 : -1.0;
  for (const Event& e : events) {
    if (e.entering == (open >= 0)) continue;  // already in that state
    if (e.entering) {
      open = e.p;
    } else {
      keep.emplace_back(open, e.p);
      open = -1.0;
    }
  }
  if (open >= 0) keep.emplace_back(open, static_cast<double>(segments));

  // The parameter is not a length — segments differ — so an interval's length
  // has to be summed over the segments it spans.
  auto lengthOf = [&](const SpineInterval& iv) {
    double total = 0.0;
    for (size_t i = 0; i < segments; ++i) {
      const double lo = std::clamp(iv.first - static_cast<double>(i), 0.0, 1.0);
      const double hi = std::clamp(iv.second - static_cast<double>(i), 0.0, 1.0);
      if (hi > lo) total += (hi - lo) * segmentLength[i];
    }
    return total;
  };

  // The length test asks what put an interval's ends where they are. A brush cut
  // at least one of them wherever the interval starts past the first station or
  // stops before the last, and the tangency artefact the debounce exists for is
  // one of those. An interval the brush cut at neither end is the whole crease,
  // selected entire: nothing there is being clipped, the crease is simply as long
  // as it is, and dropping it would make a brush that contains the whole model
  // build less than no brush at all.
  const double span = static_cast<double>(segments);
  std::vector<SpineInterval> out;
  for (const SpineInterval& iv : keep) {
    const bool cut = iv.first > 1e-9 || iv.second < span - 1e-9;
    if (cut && lengthOf(iv) < debounce) continue;
    out.push_back(iv);
  }
  return out;
}

namespace {

// The two wall triangles of edge a->b, ordered so side A/B is consistent along a
// consistently-walked chain (fixed handedness relative to the walk direction).
bool sidedTris(const MergedMesh& m, const std::map<EdgeKey, std::vector<int>>& adj, int a, int b,
               int& tA, int& tB)
{
  const auto it = adj.find(EdgeKey{std::min(a, b), std::max(a, b)});
  if (it == adj.end() || it->second.size() != 2) return false;
  tA = it->second[0];
  tB = it->second[1];
  Vector3d d = m.pos[b] - m.pos[a];
  const double len = d.norm();
  if (len < 1e-12) return false;
  d /= len;
  if (m.tris[tA].normal.cross(m.tris[tB].normal).dot(d) < 0) std::swap(tA, tB);
  return true;
}

// The point of a closed triangle nearest p: the usual region test on the
// barycentric coordinates, then the point on whichever feature (face, edge or
// corner) turned out to be nearest.
Vector3d closestPointOnTriangle(const Vector3d& p, const Vector3d& a, const Vector3d& b,
                                const Vector3d& c)
{
  const Vector3d ab = b - a, ac = c - a, ap = p - a;
  const double d1 = ab.dot(ap), d2 = ac.dot(ap);
  if (d1 <= 0 && d2 <= 0) return a;

  const Vector3d bp = p - b;
  const double d3 = ab.dot(bp), d4 = ac.dot(bp);
  if (d3 >= 0 && d4 <= d3) return b;

  const double vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + (d1 / (d1 - d3)) * ab;

  const Vector3d cp = p - c;
  const double d5 = ab.dot(cp), d6 = ac.dot(cp);
  if (d6 >= 0 && d5 <= d6) return c;

  const double vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + (d2 / (d2 - d6)) * ac;

  const double va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
    return b + ((d4 - d3) / ((d4 - d3) + (d5 - d6))) * (c - b);

  const double denom = 1.0 / (va + vb + vc);
  return a + ab * (vb * denom) + ac * (vc * denom);
}

// Distance from a point to a closed segment.
double pointSegmentDistance(const Vector3d& p, const Vector3d& a, const Vector3d& b)
{
  const Vector3d ab = b - a;
  const double len2 = ab.squaredNorm();
  if (len2 < 1e-24) return (p - a).norm();
  const double t = std::clamp(ab.dot(p - a) / len2, 0.0, 1.0);
  return (p - (a + t * ab)).norm();
}

}  // namespace

std::vector<StationNormals> chainNormals(const MergedMesh& m,
                                         const std::map<EdgeKey, std::vector<int>>& adj,
                                         const Chain& chain)
{
  auto sidedNormals = [&](int a, int b, Vector3d& nA, Vector3d& nB) {
    int tA = -1, tB = -1;
    if (!sidedTris(m, adj, a, b, tA, tB)) return false;
    nA = m.tris[tA].normal;
    nB = m.tris[tB].normal;
    return true;
  };

  // The crease between two chain parameters, reduced to one direction per wall.
  // Without resampling every span is exactly one mesh edge and this is the edge's
  // own pair of normals, which is what it has always been. With it, a span can
  // cover several raw segments and start or stop partway along one, and each
  // segment counts for as much of it as lies inside the span — so a sliver
  // counts for as little as it is long, and the ill-conditioned average that the
  // fins are made of does not arise.
  const std::vector<int>& run = chain.rawRun();
  const int rawN = chain.rawCount();
  auto spanNormals = [&](double from, double to, Vector3d& nA, Vector3d& nB) {
    if (to <= from) to += static_cast<double>(rawN);  // a ring's closing stretch
    const int first = static_cast<int>(from);
    // One whole mesh edge, asked for and answered as itself: the common case,
    // and the one an unresampled chain is entirely made of.
    if (from == static_cast<double>(first) && to == static_cast<double>(first + 1))
      return sidedNormals(run[first % rawN], run[(first + 1) % rawN], nA, nB);

    Vector3d sumA = Vector3d::Zero(), sumB = Vector3d::Zero(), eA, eB;
    for (int k = first; static_cast<double>(k) < to; ++k) {
      const int a = run[k % rawN], b = run[(k + 1) % rawN];
      if (!sidedNormals(a, b, eA, eB)) continue;
      const double lo = std::max(from - static_cast<double>(k), 0.0);
      const double hi = std::min(to - static_cast<double>(k), 1.0);
      if (hi <= lo) continue;
      const double w = (hi - lo) * (m.pos[b] - m.pos[a]).norm();
      sumA += w * eA;
      sumB += w * eB;
    }
    if (sumA.norm() < 1e-12 || sumB.norm() < 1e-12) return false;
    nA = sumA.normalized();
    nB = sumB.normalized();
    return true;
  };

  const int n = static_cast<int>(chain.verts.size());
  std::vector<StationNormals> out(n);
  for (int i = 0; i < n; ++i) {
    StationNormals s;
    s.v = chain.point(m.pos, i);

    // Average each wall's normal over the crease either side of the station
    // (with wrap-around on a closed ring); an open end has only one side. The
    // triangles the station names its walls by are the ones of the mesh edge the
    // crease arrives on, or leaves by where there is nothing before it.
    const bool hasPrev = i > 0 || chain.closed;
    const bool hasNext = i < n - 1 || chain.closed;
    const double here = chain.param(i);
    Vector3d sumA = Vector3d::Zero(), sumB = Vector3d::Zero(), nA, nB;
    const std::pair<int, int> in = chain.inEdge(i);
    const std::pair<int, int> outE = chain.outEdge(i);
    if (hasPrev && spanNormals(chain.param(i > 0 ? i - 1 : n - 1), here, nA, nB)) {
      sumA += nA;
      sumB += nB;
      if (in.first >= 0) sidedTris(m, adj, in.first, in.second, s.triA, s.triB);
    }
    if (hasNext && spanNormals(here, chain.param(i < n - 1 ? i + 1 : 0), nA, nB)) {
      sumA += nA;
      sumB += nB;
      if (s.triA < 0 && outE.first >= 0) sidedTris(m, adj, outE.first, outE.second, s.triA, s.triB);
    }
    if (sumA.norm() >= 1e-9 && sumB.norm() >= 1e-9) {
      s.nA = sumA.normalized();
      s.nB = sumB.normalized();
      s.valid = true;
    }
    out[i] = s;
  }

  return out;
}

std::vector<SpineFrame> spineFrames(const MergedMesh& m,
                                    const std::map<EdgeKey, std::vector<int>>& adj,
                                    const Chain& chain, double r, bool concave)
{
  const std::vector<StationNormals> stations = chainNormals(m, adj, chain);

  // Which way the ball sits off the crease. At a concave edge the outward wall
  // normals both point into the empty quadrant, so the center is along +(nA+nB)
  // and each tangency point lies back down its own normal; at a convex edge the
  // ball is buried in the solid and every one of those signs flips.
  const double dir = concave ? 1.0 : -1.0;

  const int n = static_cast<int>(stations.size());
  std::vector<SpineFrame> frames(n);
  for (int i = 0; i < n; ++i) {
    SpineFrame f;
    f.v = stations[i].v;
    if (!stations[i].valid) {
      frames[i] = f;
      continue;
    }
    f.nA = stations[i].nA;
    f.nB = stations[i].nB;
    f.triA = stations[i].triA;
    f.triB = stations[i].triB;

    const double phi = std::acos(std::clamp(f.nA.dot(f.nB), -1.0, 1.0));
    f.phiDeg = phi * 180.0 / M_PI;

    // As phi -> 180 the walls flatten, cos(phi/2) -> 0, and the ball center runs
    // off to infinity; leave the station without a center rather than emit a NaN.
    Vector3d bis = f.nA + f.nB;
    if (f.phiDeg > 179.0 || bis.norm() < 1e-9) {
      frames[i] = f;
      continue;
    }
    bis.normalize();
    const double d = r / std::cos(phi / 2.0);
    f.C = f.v + dir * d * bis;
    f.TA = f.C - dir * r * f.nA;
    f.TB = f.C - dir * r * f.nB;
    f.valid = true;
    frames[i] = f;
  }
  return frames;
}

std::vector<int> smoothSurfaces(const MergedMesh& m,
                                const std::map<EdgeKey, std::vector<int>>& adj,
                                double thresholdDeg)
{
  // Union-find over triangles, joined across every edge that is not a crease.
  std::vector<int> parent(m.tris.size());
  for (size_t i = 0; i < parent.size(); ++i) parent[i] = static_cast<int>(i);
  auto find = [&](int x) {
    while (parent[x] != x) x = parent[x] = parent[parent[x]];
    return x;
  };

  for (const auto& [key, ts] : adj) {
    if (ts.size() != 2) continue;
    if (isFeatureAngle(classifyEdge(m, key, m.tris[ts[0]], m.tris[ts[1]]).dihedralDeg, thresholdDeg))
      continue;
    const int a = find(ts[0]), b = find(ts[1]);
    if (a != b) parent[a] = b;
  }

  // Renumber the roots so the ids are dense and start at zero.
  std::map<int, int> idOf;
  std::vector<int> out(m.tris.size(), -1);
  for (size_t i = 0; i < out.size(); ++i) {
    const int root = find(static_cast<int>(i));
    auto [it, inserted] = idOf.try_emplace(root, static_cast<int>(idOf.size()));
    out[i] = it->second;
  }
  return out;
}

std::vector<ChainContact> chainContacts(const MergedMesh& m,
                                        const std::map<EdgeKey, std::vector<int>>& adj,
                                        const Chain& chain, double size, bool concave, bool wedge,
                                        const std::vector<int>& surfaceOf, int samplesPerSegment)
{
  const std::vector<StationNormals> stations = chainNormals(m, adj, chain);
  const double dir = concave ? 1.0 : -1.0;

  // The walls, as surfaces rather than triangles: which triangles each is made
  // of, and the segments that bound it. A wall's boundary is the creases around
  // it — including the one being blended — so a ball whose nearest point on a
  // wall lands on one of those segments has reached the end of that wall.
  std::vector<std::vector<int>> surfaceTris;
  for (size_t t = 0; t < surfaceOf.size() && t < m.tris.size(); ++t) {
    if (surfaceOf[t] < 0) continue;
    if (static_cast<size_t>(surfaceOf[t]) >= surfaceTris.size())
      surfaceTris.resize(surfaceOf[t] + 1);
    surfaceTris[surfaceOf[t]].push_back(static_cast<int>(t));
  }
  std::vector<std::vector<EdgeKey>> surfaceRim(surfaceTris.size());
  for (const auto& [key, ts] : adj) {
    std::set<int> touching;
    for (const int t : ts)
      if (static_cast<size_t>(t) < surfaceOf.size() && surfaceOf[t] >= 0)
        touching.insert(surfaceOf[t]);
    // Two triangles of the same surface make an interior seam; anything else —
    // a crease, a non-manifold edge, a border — ends the surface.
    if (ts.size() == 2 && touching.size() == 1) continue;
    for (const int s : touching)
      if (static_cast<size_t>(s) < surfaceRim.size()) surfaceRim[s].push_back(key);
  }

  // How far a wall may turn away from the triangle the question was asked at
  // before it stops being that wall. A wall curved enough to matter still turns
  // only by its sagitta over the tool's own footprint — a radius-2 blend on a
  // radius-10 boss covers 27 degrees of it — while a bead turns by the whole
  // crease angle within a couple of millimetres, which is what separates the two
  // without anything having to be told which pass built what.
  //
  // A right angle is exactly the wrong value, and measurably so: at 90 the walk
  // steps from a rib's side onto the plate its bead lands on, which is at
  // precisely 90, and four creases are lost again. Anything from 30 to 85 gives
  // the same answer on every model in the set.
  constexpr double kWallTurnDeg = 60.0;
  const double turnCap = std::cos(kWallTurnDeg * M_PI / 180.0);

  // The nearest point of the wall to `p`, where the wall is the part of the
  // surface this crease can actually reach: walked from the triangle the station
  // named, never leaving the surface, never turning further than the cap above,
  // and never stepping further from the crease than `budget` — which is as far
  // as a point the seated ball touches can possibly be.
  //
  // Asking the whole surface instead is right until something has been blended
  // into the target, and wrong the moment one has been. A bead is tangent to
  // both walls it touches, which is what a fillet is, so the smooth grouping
  // runs straight through it: a rib with a bead at its foot comes back as ONE
  // surface — near side, both beads, the plate and the far side. The nearest
  // point of that to a ball seated on the rib's top corner is on the face
  // opposite, and every question asked of the contact afterwards is then asked
  // about the wrong wall.

  auto nearestOnWall = [&](const Vector3d& p, const Vector3d& from, int startTri, int surface,
                           double budget, Vector3d *onWall) {
    double best = std::numeric_limits<double>::infinity();
    if (surface < 0 || static_cast<size_t>(surface) >= surfaceTris.size()) return best;
    if (startTri < 0 || static_cast<size_t>(startTri) >= m.tris.size()) return best;

    std::set<int> seen{startTri};
    std::vector<int> stack{startTri};
    while (!stack.empty()) {
      const int t = stack.back();
      stack.pop_back();
      const Tri& tri = m.tris[t];
      const Vector3d& a = m.pos[tri.v[0]];
      const Vector3d& b = m.pos[tri.v[1]];
      const Vector3d& c = m.pos[tri.v[2]];

      const Vector3d q = closestPointOnTriangle(p, a, b, c);
      if ((p - q).norm() < best) {
        best = (p - q).norm();
        if (onWall) *onWall = q;
      }

      // Measured, and walked, from the crease: a triangle out of the tool's
      // reach is still the nearest thing to `p` if nothing closer exists, but
      // nothing past it is reachable through it.
      if ((closestPointOnTriangle(from, a, b, c) - from).norm() > budget) continue;

      for (int k = 0; k < 3; ++k) {
        const auto it = adj.find(std::minmax(tri.v[k], tri.v[(k + 1) % 3]));
        if (it == adj.end()) continue;
        for (const int nb : it->second)
          if (static_cast<size_t>(nb) < surfaceOf.size() && surfaceOf[nb] == surface &&
              m.tris[nb].normal.dot(m.tris[startTri].normal) > turnCap && seen.insert(nb).second)
            stack.push_back(nb);
      }
    }
    return best;
  };

  // Where the ball really touches one of its walls, and whether that is a touch
  // at all. Stepping off the ball centre along an averaged wall normal — the
  // construction the sections themselves use — assumes the wall is flat, and on
  // a doubly curved one the point it produces sits off the surface by the
  // sagitta, r^2/2R, however finely the wall is tessellated: a bead on a dome
  // would be refused for a miss that is an artefact of the construction. Asking
  // the wall for its nearest point to the centre instead puts the contact on the
  // wall by construction, curved or not.
  //
  // That also states the question exactly rather than as a distance against a
  // tolerance: the blend leaves the surface it is meant to meet precisely when
  // the nearest point is on the wall's boundary rather than inside it, because
  // then the wall has ended and the ball is hanging off it. The boundary is part
  // of the wall, so its distance is never less than the wall's own; equal is
  // what says the contact sits on it.
  //
  // `turned` says the chain changes walls on this side at this station — the
  // spine's own corner. There the averaged normal is the average of two
  // different walls' normals, so the point it seats lands on the crease between
  // them: on the boundary of each, by construction and at any size. That is the
  // ball rolling from one wall onto the next, which is what a chain that turns
  // is, and not a wall running out. The question is left to the samples either
  // side, which each ask about one wall.
  auto seatOn = [&](ChainContact& c, const Vector3d& n, int surface, int tri, Vector3d& T,
                    bool turned) {
    // How far from the crease a point this ball touches can be: out to the
    // centre, and a radius further. Past that is another feature's wall, however
    // smoothly the mesh gets there.
    const double budget = (c.C - c.v).norm() + c.radius;
    Vector3d onWall;
    const double d = nearestOnWall(c.C, c.v, tri, surface, budget, &onWall);
    if (!std::isfinite(d)) return;  // no wall to ask; leave the constructed point
    T = onWall;
    if (turned) return;

    double rim = std::numeric_limits<double>::infinity();
    for (const EdgeKey& e : surfaceRim[surface])
      rim = std::min(rim, pointSegmentDistance(c.C, m.pos[e.first], m.pos[e.second]));
    if (rim > d + 1e-9 * std::max(1.0, d)) return;

    // Hanging off the end of this wall. What the user can act on is how far past
    // it the blend would stop, so report the miss of the point the bead would
    // actually be built to.
    c.offFace = std::max(
      c.offFace, nearestOnWall(c.C - dir * c.radius * n, c.v, tri, surface, budget, nullptr));
  };

  auto contactAt = [&](const StationNormals& s, int vert, bool turnedA = false,
                       bool turnedB = false) {
    ChainContact c;
    c.v = s.v;
    c.vert = vert;
    if (!s.valid) return c;

    const double cosPhi = std::clamp(s.nA.dot(s.nB), -1.0, 1.0);
    const double phi = std::acos(cosPhi);
    Vector3d bis = s.nA + s.nB;
    // Flat walls have no crease to blend, and the seated ball runs to infinity.
    if (phi > 179.0 * M_PI / 180.0 || bis.norm() < 1e-9) return c;
    bis.normalize();

    // The wedge tools take the setback directly, so the ball that touches the
    // walls there has whatever radius puts its tangency points at that setback:
    // t = r*tan(phi/2) read the other way round.
    const double r = wedge ? size / std::tan(phi / 2.0) : size;
    c.radius = r;
    c.C = s.v + dir * (r / std::cos(phi / 2.0)) * bis;
    c.TA = c.C - dir * r * s.nA;
    c.TB = c.C - dir * r * s.nB;
    if (s.triA >= 0 && static_cast<size_t>(s.triA) < surfaceOf.size())
      c.surfaceA = surfaceOf[s.triA];
    if (s.triB >= 0 && static_cast<size_t>(s.triB) < surfaceOf.size())
      c.surfaceB = surfaceOf[s.triB];
    c.valid = std::isfinite(r) && c.C.allFinite();
    if (c.valid) {
      seatOn(c, s.nA, c.surfaceA, s.triA, c.TA, turnedA);
      seatOn(c, s.nB, c.surfaceB, s.triB, c.TB, turnedB);
    }
    return c;
  };

  // Which stretches of the chain are being built. The brushes narrow a crease to
  // the parts they cover, and the question of whether the blend still meets its
  // walls is only about the parts there is a bead on: the far half of a crease
  // running off the end of its face says nothing about the near half, which is
  // all the user asked for. Station i sits at parameter i, and a sample partway
  // along segment i at parameter i + t; an empty `keep` is the whole chain.
  auto isBuilt = [&](double param) {
    if (chain.keep.empty()) return true;
    for (const SpineInterval& iv : chain.keep)
      if (param >= iv.first && param <= iv.second) return true;
    return false;
  };

  const size_t n = stations.size();
  const size_t segments = n < 2 ? 0 : (chain.closed ? n : n - 1);

  // Which walls the chain has either side of it as it arrives at a station and
  // as it leaves — the same sided convention the normals are averaged under, so
  // the two can be compared side by side. A station with only one incident edge
  // has nothing to turn between.
  auto sideSurfaces = [&](int a, int b, int& sA, int& sB) {
    sA = sB = -1;
    int tA = -1, tB = -1;
    if (!sidedTris(m, adj, a, b, tA, tB)) return false;
    if (tA >= 0 && static_cast<size_t>(tA) < surfaceOf.size()) sA = surfaceOf[tA];
    if (tB >= 0 && static_cast<size_t>(tB) < surfaceOf.size()) sB = surfaceOf[tB];
    return true;
  };

  std::vector<ChainContact> out;
  out.reserve(n * (1 + std::max(samplesPerSegment, 0)));
  for (size_t i = 0; i < n; ++i) {
    // A point the brushes left out is carried as an invalid placeholder rather
    // than left out of the list, so the ends of the list are still the ends of
    // the chain — which is what the callers that exempt them are asking about.
    ChainContact station;
    if (isBuilt(static_cast<double>(i))) {
      bool turnedA = false, turnedB = false;
      // The walls the crease arrives on and leaves by. At an interpolated
      // station both are the one raw segment it lies inside, so it never reads
      // as a corner — which is right: a point in the middle of a segment has no
      // turn in it.
      const std::pair<int, int> inE = chain.inEdge(static_cast<int>(i));
      const std::pair<int, int> outE = chain.outEdge(static_cast<int>(i));
      int inA = -1, inB = -1, outA = -1, outB = -1;
      if (inE.first >= 0 && outE.first >= 0 && sideSurfaces(inE.first, inE.second, inA, inB) &&
          sideSurfaces(outE.first, outE.second, outA, outB)) {
        turnedA = inA != outA;
        turnedB = inB != outB;
      }
      station = contactAt(stations[i], chain.verts[i], turnedA, turnedB);
    } else {
      station.v = stations[i].v;
      station.vert = chain.verts[i];
    }
    out.push_back(std::move(station));
    if (i >= segments || samplesPerSegment <= 0) continue;

    // Between two stations the walls turn from one pair of normals to the
    // other; interpolating them is what the cell between the two sections is
    // built from, so it is the same shape being asked about. The walls
    // themselves are the ones of the segment's own edge — at a corner of a
    // closed loop the stations' are two different pairs, and only the segment's
    // is the surface a point on it can be expected to lie on.
    const StationNormals& a = stations[i];
    const StationNormals& b = stations[(i + 1) % n];
    if (!a.valid || !b.valid) continue;
    int segA = -1, segB = -1;
    const std::pair<int, int> segEdge =
      chain.rawMid(static_cast<int>(i), static_cast<int>((i + 1) % n));
    if (!sidedTris(m, adj, segEdge.first, segEdge.second, segA, segB)) continue;

    // One sample per size along the segment, and never fewer than asked. A fixed
    // count is a trap on a long crease: the room a spike leaves its tool runs
    // out somewhere between the last sample and the vertex, and where that is
    // depends on the size, so the walk has to be as fine as the size is small.
    // The cap is there because a crease can be arbitrarily long next to a tool
    // that is arbitrarily small, and the check is quadratic in its samples.
    const double segmentLength = (b.v - a.v).norm();
    const int samples =
      std::clamp(static_cast<int>(std::ceil(segmentLength / std::max(size, 1e-12))),
                 std::max(samplesPerSegment, 0), 32);
    for (int k = 1; k <= samples; ++k) {
      const double t = static_cast<double>(k) / (samples + 1);
      StationNormals s;
      s.v = a.v + t * (b.v - a.v);
      s.nA = (1.0 - t) * a.nA + t * b.nA;
      s.nB = (1.0 - t) * a.nB + t * b.nB;
      s.triA = segA;
      s.triB = segB;
      if (s.nA.norm() < 1e-9 || s.nB.norm() < 1e-9) continue;
      s.nA.normalize();
      s.nB.normalize();
      s.valid = true;
      if (!isBuilt(static_cast<double>(i) + t)) {
        ChainContact skipped;
        skipped.v = s.v;
        out.push_back(std::move(skipped));
        continue;
      }
      out.push_back(contactAt(s, -1));
    }
  }
  return out;
}

double pointTriangleDistance(const Vector3d& p, const Vector3d& a, const Vector3d& b,
                             const Vector3d& c)
{
  return (p - closestPointOnTriangle(p, a, b, c)).norm();
}

// The same chain, parameterised by the crease as the mesh has it: one station
// per crease vertex, and the brushes' intervals carried over to that parameter.
//
// Whether a size fits is a question about the target and the size, and about
// nothing else; where the stations happen to sit is not part of it. Re-dividing
// a crease at equal arc length steps over its slivers by design, so a blend that
// leaves its wall only across one would go unsampled and be accepted. Asking the
// crease itself keeps one answer per target, whatever the stations do.
namespace {
Chain rawStationChain(const Chain& chain)
{
  if (chain.at.empty()) return chain;

  Chain out;
  out.raw = chain.rawRun();
  out.verts = out.raw;
  out.closed = chain.closed;

  const int nsta = static_cast<int>(chain.at.size());
  const double nraw = static_cast<double>(out.raw.size());
  // Station parameter -> crease parameter: the piecewise-linear map `at` is,
  // read forwards. A ring's parameter runs one past its last station, onto the
  // closing segment, and that end is the crease's own length.
  auto toRaw = [&](double q) {
    const double last = chain.closed ? static_cast<double>(nsta) : static_cast<double>(nsta - 1);
    q = std::clamp(q, 0.0, last);
    if (q >= last) return chain.closed ? nraw : nraw - 1.0;
    const int i = static_cast<int>(q);
    const double f = q - static_cast<double>(i);
    const double a = chain.at[i];
    double b = i + 1 < nsta ? chain.at[i + 1] : nraw;
    if (b <= a) b = nraw;
    return a + f * (b - a);
  };

  for (const SpineInterval& iv : chain.keep) {
    const SpineInterval mapped{toRaw(iv.first), toRaw(iv.second)};
    if (mapped.second - mapped.first > 1e-12) out.keep.push_back(mapped);
  }
  // A brush that covers nothing has to stay covering nothing: an empty `keep` is
  // read as the whole chain, so a stretch that maps away leaves a point rather
  // than the lot.
  if (out.keep.empty() && !chain.keep.empty()) out.keep.emplace_back(0.0, 0.0);
  return out;
}
}  // namespace

std::vector<SizeVerdict> checkChainSizes(const MergedMesh& m,
                                         const std::map<EdgeKey, std::vector<int>>& adj,
                                         const std::vector<Chain>& chains, double size,
                                         bool concave, bool wedge, double thresholdDeg)
{
  std::vector<SizeVerdict> verdicts(chains.size());
  if (!(size > 0) || chains.empty()) return verdicts;

  const std::vector<int> surfaceOf = smoothSurfaces(m, adj, thresholdDeg);

  std::vector<std::vector<ChainContact>> contacts;
  contacts.reserve(chains.size());
  for (const Chain& chain : chains) {
    const Chain asMeshed = rawStationChain(chain);
    contacts.push_back(chainContacts(m, adj, asMeshed, size, concave, wedge, surfaceOf,
                                     /*samplesPerSegment=*/3));
  }

  // The crowding question is asked of where contact points sit against each
  // other, and those sit on a tessellated wall: each is within about half a seam
  // angle of where the smooth surface would put it. The crease threshold is the
  // largest seam the tessellation can produce, which makes it the bound on that,
  // and it also swallows the float noise the plan asks be clamped silently
  // rather than dropped. It widens the region asked about rather than narrowing
  // it, so the doubtful case is refused: the alternative is accepting a size at
  // which two beads just touch, which is the tangential contact that leaves
  // slivers behind.
  const double faceTol =
    std::max(size * (1.0 - std::cos(thresholdDeg * M_PI / 180.0)), 1e-9 * size);

  // Asked of the crease as the mesh has it, not of the stations: two chains meet
  // at a mesh vertex, and a resampled chain's stations are not all of them. The
  // two are the same list until something has been resampled.
  std::vector<std::set<int>> chainVerts(chains.size());
  for (size_t ci = 0; ci < chains.size(); ++ci)
    chainVerts[ci].insert(chains[ci].rawRun().begin(), chains[ci].rawRun().end());

  // A box around each chain's contact points, so the crowding question can skip
  // the pairs that are nowhere near each other.
  struct Reach
  {
    Vector3d lo = Vector3d::Zero();
    Vector3d hi = Vector3d::Zero();
    bool empty = true;
    void add(const Vector3d& p)
    {
      lo = empty ? p : lo.cwiseMin(p);
      hi = empty ? p : hi.cwiseMax(p);
      empty = false;
    }
    bool contains(const Vector3d& p, double slack) const
    {
      return !empty && (p.array() >= lo.array() - slack).all() &&
             (p.array() <= hi.array() + slack).all();
    }
  };
  std::vector<Reach> reach(chains.size());
  for (size_t ci = 0; ci < chains.size(); ++ci)
    for (const ChainContact& c : contacts[ci]) {
      if (!c.valid) continue;
      reach[ci].add(c.TA);
      reach[ci].add(c.TB);
    }

  // Vertices where three or more creases meet, and how much crease either side of
  // one the touching question does not apply to. The spine is cut back short of a
  // junction and a corner cell takes over, so contacts inside that stretch are
  // not contacts the tool has — and near one they read wrong for a reason that
  // has nothing to do with the size: stepping in perpendicular to a crease from a
  // point close to where it meets another leaves through THAT crease's face,
  // which is a corner and not an overshoot.
  std::map<int, int> endsAt;
  for (const Chain& chain : chains) {
    if (chain.closed || chain.verts.size() < 2) continue;
    ++endsAt[chain.verts.front()];
    ++endsAt[chain.verts.back()];
  }
  std::vector<Vector3d> junctionPos;
  for (const auto& [v, count] : endsAt)
    if (count >= 3) junctionPos.push_back(m.pos[v]);
  const double junctionReach = 2.0 * size;

  for (size_t ci = 0; ci < chains.size(); ++ci) {
    SizeVerdict& verdict = verdicts[ci];

    // Does the tool still touch the walls it is blending? A ball whose nearest
    // point on a wall is on that wall's boundary has reached the end of it, and
    // no blend of the size asked for exists there at all.
    //
    // Not at the two ends of an open chain, though. A crease that stops does so
    // at the boundary of its own walls — at a junction, or where the feature
    // simply runs out — so the contact point at that last station sits in the
    // corner of the wall and steps out of it for reasons that have nothing to do
    // with the size. On a tetrahedron it is unmissable: the base triangle's
    // corners are 60 degrees, so stepping perpendicular to one base edge leaves
    // through the next. A size that genuinely does not fit fails along the
    // crease, not only at its ends, and the samples in between are what say so.
    //
    // Both exemptions are stated as narrowings of the question, and they are
    // only that while something is left to ask. A crease shorter than the
    // junction reach has no sample that is neither an end nor beside a
    // junction, so on that crease the exemptions do not narrow the question,
    // they delete it — and the shape that produces such creases in bulk is a
    // bead's runout lip, which arrives already broken into dozens of two- and
    // three-vertex chains meeting each other inside one reach. The tally below
    // is what tells the two apart: where nothing at all was tested, the
    // evidence that was thrown away is all there is, and it is used.
    const size_t last = contacts[ci].empty() ? 0 : contacts[ci].size() - 1;
    int nExempt = 0, nTested = 0;
    double offExempt = 0.0, offTested = 0.0;
    double coordMag = 0.0;
    Vector3d exemptAt = Vector3d::Zero();
    for (size_t i = 0; i < contacts[ci].size(); ++i) {
      const ChainContact& c = contacts[ci][i];
      if (!c.valid) continue;
      // How big the numbers being subtracted are, along this crease. A miss is
      // a nearest point and a wall's boundary taken away from each other, so
      // whatever the subtraction cannot resolve is a fraction of these, and of
      // nothing else.
      coordMag = std::max({coordMag, c.v.cwiseAbs().maxCoeff(), c.TA.cwiseAbs().maxCoeff(),
                           c.TB.cwiseAbs().maxCoeff()});
      bool exempt = !chains[ci].closed && (i == 0 || i == last);
      if (!exempt)
        for (const Vector3d& p : junctionPos)
          if ((c.v - p).norm() < junctionReach) { exempt = true; break; }
      if (exempt) {
        ++nExempt;
        if (c.offFace > offExempt) { offExempt = c.offFace; exemptAt = c.v; }
        continue;
      }
      ++nTested;
      offTested = std::max(offTested, c.offFace);
      // A miss of zero is not a miss. Where the contact lands exactly on the
      // wall's boundary the blend stops precisely at the edge of the wall it is
      // meant to meet, which is a fit — and the two are told apart by a double's
      // last bits, since the nearest point and the boundary are then the same
      // point computed two ways. The margin is far below anything a mesh could
      // mean and six orders above that noise; a size that really does not fit
      // misses by a fraction of itself, never by a nanometre.
      if (c.offFace > 1e-9 * std::max(1.0, size)) {
        verdict = {SizeFault::OffFace, c.v, c.offFace};
        break;
      }
    }

    // The margin here is not the one the tested samples use, and it must not be.
    // A tested sample sits in the interior of a wall, where its miss is either
    // exactly zero or macroscopic, so a nanometre tells the two apart. Every
    // exempt sample sits *on* a wall's boundary — that is what made it exempt —
    // and there the miss is the nearest point and the boundary subtracted from
    // each other, two computations of one point. That difference is not zero and
    // it would be read as geometry, differently on two edges of the same
    // bracket, so a floor has to go under it.
    //
    // Two quantities set that floor and they are independent, so it is the
    // larger of them. A crease that genuinely cannot carry the size misses by a
    // fraction *of the size*: measured misses on the shape this rule exists for
    // run from 2e-4 of the size upward, so a ten-thousandth of the size sits
    // below every real miss. What the subtraction cannot resolve, though, has
    // nothing to do with the size — it is a fraction of the magnitude of the
    // coordinates being subtracted. Measured on right-angle box models, whose
    // creases are exact so that every non-zero answer is arithmetic: across 288
    // runs and 984 such creases, at coordinate magnitudes from 40 to 1e4, the
    // largest spurious miss is 2.4e-8 mm at 40 and 2.0e-6 mm at 1e4, and never
    // more than 6e-10 of the coordinate magnitude. A ten-millionth of it clears
    // that by about 170, and it is not set higher because too low a floor costs
    // a refusal — a valid solid with an unfilleted crease — while too high a one
    // costs the shattered solid this gate exists to stop.
    //
    // Past a coordinate magnitude of about 1e4 the evidence on those same exact
    // creases stops looking like round-off at all: it reaches 0.8 mm at 1e5 and
    // 1.2 mm at 1e6, a large fraction of the size asked for, because the mesh
    // the question is asked of has itself degraded. This term does not cover
    // that and should not pretend to. What happens there is a refusal, which is
    // the safe answer to a question that can no longer be asked.
    //
    // Both terms scale with the model, which is what a modeller without units
    // requires: the same shape at any scale gets the same verdicts. An absolute
    // millimetre would not, and the coordinate term is also what keeps a floor
    // under a very small size, which is the job an absolute one was doing badly.
    const double blindFloor = std::max(1e-4 * size, 1e-7 * coordMag);
    // A crease on which the two exemptions discarded every sample is asked about
    // anyway, from the samples they discarded. A crease that has nothing to test
    // also has nothing to report, so this is quiet wherever the gate had a
    // testable sample; it only speaks where every sample was exempt and the
    // discarded ones miss their wall by more than the floor above. Passing such
    // a crease unexamined answers "I could not look" as if it were "it fits".
    if (verdict.fault == SizeFault::Fits && nTested == 0 && nExempt > 0 &&
        offExempt > blindFloor) {
      verdict = {SizeFault::OffFace, exemptAt, offExempt};
    }

    if (verdict.fault != SizeFault::Fits) continue;

    // Is the room it needs its own? Another crease's contact line inside the
    // material this blend uses is that crease's bead being eaten. Creases that
    // meet at a junction are exempt: sharing the material there is what a corner
    // cell is.
    //
    // The material the blend uses is *not* the seated ball. The ball is tangent
    // to each wall and goes on reaching along that wall for another radius past
    // where it touches, into material the finished blend never comes near — the
    // blend is only the corner between the two tangency lines. Asking the ball
    // refuses two beads that share a face whenever the face is narrower than
    // three radii, where they in fact fit until it is narrower than two: on a
    // cube every radius past a third of the side is refused and every one up to
    // half of it is buildable. So ask whether the other crease's contact point
    // lands in the corner region itself.
    for (const ChainContact& c : contacts[ci]) {
      if (!c.valid) continue;

      // The corner region, bounded the way the tool is: between each wall and
      // the tangency line on it, and no further from the ball centre than the
      // crease itself. `iA` and `iB` are the unit directions from the centre to
      // where it touches each wall, reversed — so they point the way the tool
      // lies, into the material for a round and into the air for a fillet, and
      // a contact point's component along one is its depth under that wall.
      const Vector3d iA = (c.C - c.TA).normalized();
      const Vector3d iB = (c.C - c.TB).normalized();
      if (!iA.allFinite() || !iB.allFinite()) continue;
      // How deep under a wall the corner reaches. At a right angle that is the
      // radius, at a sharper crease more — the tool runs out along the *other*
      // wall to a point this far under this one — and at a shallow one less.
      const double deep = c.radius * (1.0 - std::clamp(iA.dot(iB), -1.0, 1.0));
      const double apex = (c.C - c.v).norm();

      for (size_t cj = 0; cj < chains.size() && verdict.fault == SizeFault::Fits; ++cj) {
        if (cj == ci) continue;
        // Everything this corner can reach is inside that chain's own box or not
        // at issue; without the test the question is asked of every pair of
        // contact points on the model.
        if (!reach[cj].contains(c.C, apex)) continue;
        bool meets = false;
        for (const int v : chains[cj].rawRun())
          if (chainVerts[ci].count(v)) { meets = true; break; }
        if (meets) continue;

        for (const ChainContact& o : contacts[cj]) {
          if (!o.valid) continue;
          for (const Vector3d& T : {o.TA, o.TB}) {
            if ((c.C - T).norm() > apex + faceTol) continue;
            const double underA = (T - c.TA).dot(iA);
            const double underB = (T - c.TB).dot(iB);
            if (underA < -faceTol || underA > deep + faceTol) continue;
            if (underB < -faceTol || underB > deep + faceTol) continue;
            // What the user can act on is how far off this crease the feature
            // competing with it sits, not where the ball's centre happened to
            // land.
            verdict = {SizeFault::Crowded, c.v, (c.v - T).norm()};
            break;
          }
          if (verdict.fault != SizeFault::Fits) break;
        }
      }
      if (verdict.fault != SizeFault::Fits) break;
    }
  }

  return verdicts;
}

namespace {

// The wedge cross-section shared by every tool: the two setback points, then the
// same corner pushed past each wall so the tool crosses it transversally rather
// than lying coplanar with it. Each primed point is displaced along its *own*
// wall normal, and by that wall's own overshoot — a station standing clear of a
// flat wall and a curved one at once has two distances to keep, and wallOvershoot
// says where each comes from. The point on the bisector takes the larger: at a
// crease the material is the union of two half-spaces, so a point only has to be
// behind one of them, and behind the deeper wall it is behind both.
// `dir` is +1 for a concave tool and -1 for a convex one.
std::array<Vector3d, 5> pentagonSection(const Vector3d& v, const Vector3d& nA, const Vector3d& nB,
                                        const Vector3d& bis, const Vector3d& TA,
                                        const Vector3d& TB, double dir, double epsA, double epsB)
{
  return {TA, TB, TB - dir * epsB * nB, v - dir * std::max(epsA, epsB) * bis,
          TA - dir * epsA * nA};
}

// How far past a wall the tool has to stand at one station: the fixed hair, plus
// however far that wall has fallen away from the plane the hair is measured in.
//
// The overshoot is stepped off the crease point along the station's own averaged
// wall normal, which puts the tool's wall face in a plane tangent to the wall
// there. A flat wall stays in that plane and a hair is enough, which is why a
// fixed one ever worked. A curved wall falls away from it — by the sagitta over
// the setback, which on a coarsely tessellated pipe is twenty times the hair —
// so the face clears the wall at the station it was measured at and stands proud
// of it in between. What that leaves in the finished solid is a ledge one
// overshoot deep along the whole tangency line, with the tool's own faces on
// both sides of it, and nothing downstream can tell it from a crease of the
// shape: it is read as a wall wanting rounding.
//
// The distance is asked at the tangency point, which is the far edge of the
// tool's footprint and so the deepest the wall gets under it. The wall itself is
// walked from the triangle the station named, out to the tangency point and no
// further, and it stops at the first crease: where one wall ends is the question
// `isFeatureAngle` already answers, and a tangency point that has run off the
// end of its wall is the size gate's business, not this one's — measured here it
// would read the next wall along as a dip and bury the tool in it.
double wallOvershoot(const MergedMesh& m, const std::map<EdgeKey, std::vector<int>>& adj,
                     const Vector3d& v, const Vector3d& n, int tri, const Vector3d& T, double eps,
                     double thresholdDeg)
{
  const double reach = (T - v).norm();
  if (tri < 0 || static_cast<size_t>(tri) >= m.tris.size() || !(reach > 0)) return eps;

  double best = std::numeric_limits<double>::infinity();
  Vector3d onWall = T;
  std::set<int> seen{tri};
  std::vector<int> stack{tri};
  while (!stack.empty()) {
    const Tri& t = m.tris[stack.back()];
    stack.pop_back();
    const Vector3d& a = m.pos[t.v[0]];
    const Vector3d& b = m.pos[t.v[1]];
    const Vector3d& c = m.pos[t.v[2]];

    const Vector3d q = closestPointOnTriangle(T, a, b, c);
    if ((T - q).norm() < best) {
      best = (T - q).norm();
      onWall = q;
    }

    // A triangle the tool does not stand on is measured — it may still be the
    // nearest thing to the tangency point — but not walked through, so the walk
    // stays the size of the footprint however large the surface is.
    if ((closestPointOnTriangle(v, a, b, c) - v).norm() > reach) continue;

    for (int k = 0; k < 3; ++k) {
      const EdgeKey key = std::minmax(t.v[k], t.v[(k + 1) % 3]);
      const auto it = adj.find(key);
      if (it == adj.end() || it->second.size() != 2) continue;
      if (isFeatureAngle(classifyEdge(m, key, m.tris[it->second[0]], m.tris[it->second[1]])
                           .dihedralDeg,
                         thresholdDeg))
        continue;
      for (const int nb : it->second)
        if (seen.insert(nb).second) stack.push_back(nb);
    }
  }
  // Under the tangency plane, never over it: a wall that curves toward the tool
  // is already crossed by the fixed hair, and shortening it would leave the tool
  // resting on the wall instead of crossing it.
  return eps + std::max(0.0, (v - onWall).dot(n));
}

// Consecutive cells meet along a shared section face, so a union of them has
// inputs that touch on a set of zero measure. Manifold copes, but it leaves one
// degenerate four-triangle shell behind per such contact: no volume, no effect
// on any boolean, and a nonsense genus for anything that inspects the result.
// Keep only the components that enclose material.
manifold::Manifold dropVolumelessParts(manifold::Manifold solid)
{
  std::vector<manifold::Manifold> parts = solid.Decompose();
  if (parts.size() < 2) return solid;

  const double keepAbove = 1e-9 * std::abs(solid.Volume());
  std::vector<manifold::Manifold> solidParts;
  for (auto& part : parts)
    if (std::abs(part.Volume()) > keepAbove) solidParts.push_back(std::move(part));

  if (solidParts.empty()) return {};
  if (solidParts.size() == 1) return solidParts.front();
  if (solidParts.size() == parts.size()) return solid;
  return manifold::Manifold::BatchBoolean(solidParts, manifold::OpType::Add);
}

// Union in a tree, sweeping each pair as it is made. Decompose() materialises a
// mesh of its own per component, so the sweep costs (components x mesh size),
// and a tool carries one shell per cell contact: sweeping the finished union
// instead is quadratic, and cost 112 s and 17 GB on a plate of a hundred bosses.
// A pair can leave only the one shell between its two members, so the tree hands
// Decompose two components a step whatever the tool's size.
manifold::Manifold unionCells(std::vector<manifold::Manifold>& cells)
{
  if (cells.empty()) return {};

  std::vector<manifold::Manifold> merged;
  while (cells.size() > 1) {
    merged.clear();
    merged.reserve((cells.size() + 1) / 2);
    for (size_t i = 0; i + 1 < cells.size(); i += 2)
      merged.push_back(dropVolumelessParts(cells[i] + cells[i + 1]));
    if (cells.size() % 2) merged.push_back(cells.back());
    cells.swap(merged);
  }
  return cells.front();
}

// A crease vertex a cell has to carry, and how far off the cell's own chord it
// lies. A cell is the hull of the sections at its two ends, so what it covers
// along the crease is the straight chord between them. While one section stands
// at every crease vertex that chord IS the crease and nothing can be missed; as
// soon as the stations are re-divided a chord can span a crease vertex and pass
// inside it, and the sliver of material out at that vertex is a hair outside the
// tool. `s` is where the vertex falls along the cell, `off` the vector from the
// chord to it.
struct SpineBulge
{
  double s = 0.0;
  Vector3d off = Vector3d::Zero();
};

// The direction a tool at this crease vertex may grow in without taking
// material it was never asked for. Face normals point out of the solid, so their
// sum at the vertex points out of it too: a round is outside its crease and may
// grow that way, a fillet is inside the notch and may grow the other. Zero where
// the crease's own segments cannot be walked.
Vector3d wallsAway(const MergedMesh& m, const std::map<EdgeKey, std::vector<int>>& adj,
                   const std::vector<int>& run, size_t j, bool closed, bool concave)
{
  const size_t n = run.size();
  Vector3d sum = Vector3d::Zero();
  auto add = [&](int a, int b) {
    int tA = -1, tB = -1;
    if (!sidedTris(m, adj, a, b, tA, tB)) return;
    sum += m.tris[tA].normal + m.tris[tB].normal;
  };
  if (j > 0) add(run[j - 1], run[j]);
  else if (closed && n > 1) add(run[n - 1], run[0]);
  if (j + 1 < n) add(run[j], run[j + 1]);
  else if (closed && n > 1) add(run[n - 1], run[0]);
  if (sum.norm() < 1e-12) return Vector3d::Zero();
  return (concave ? -1.0 : 1.0) * sum.normalized();
}

// Every crease vertex each cell's chord passes inside of. Indexed by cell, which
// is by leading section, so it lines up with the loop in appendChainCells.
//
// `sectionAt` is where each section sits in chain-parameter space. The result is
// empty for a chain nobody resampled: there every station is a crease vertex, so
// no chord can span one, and the cells are exactly what they always were.
std::vector<std::vector<SpineBulge>> chainBulges(const MergedMesh& m,
                                                 const std::map<EdgeKey, std::vector<int>>& adj,
                                                 const Chain& chain,
                                                 const std::vector<double>& sectionAt, bool concave)
{
  const size_t nsec = sectionAt.size();
  if (chain.at.empty() || nsec < 2) return {};

  const std::vector<int>& run = chain.rawRun();
  const double nraw = static_cast<double>(run.size());
  const double nsta = static_cast<double>(chain.at.size());

  // Chain parameter -> crease parameter, and -> the point the sections are
  // lofted between, which is the chord of the two stations either side rather
  // than the crease itself.
  auto rawParam = [&](double q) {
    const double k = std::floor(q);
    const double f = q - k;
    const int i = static_cast<int>(k);
    const double a = chain.param(i % static_cast<int>(nsta));
    double b = chain.param((i + 1) % static_cast<int>(nsta));
    if (b <= a) b += nraw;
    return a + f * (b - a);
  };
  auto stationPoint = [&](double q) {
    const double k = std::floor(q);
    const double f = q - k;
    const int i = static_cast<int>(k);
    const Vector3d& a = chain.point(m.pos, i % static_cast<int>(nsta));
    const Vector3d& b = chain.point(m.pos, (i + 1) % static_cast<int>(nsta));
    return (a + f * (b - a)).eval();
  };
  // Crease parameter -> chain parameter: the inverse of the piecewise-linear map
  // above, which is what puts the vertex at the right fraction of the cell.
  auto chainParam = [&](double p) {
    for (int i = 0; i < static_cast<int>(nsta); ++i) {
      const double a = chain.param(i);
      double b = chain.param((i + 1) % static_cast<int>(nsta));
      if (b <= a) b += nraw;
      if (p >= a - 1e-12 && p <= b + 1e-12 && b > a)
        return static_cast<double>(i) + (p - a) / (b - a);
    }
    return -1.0;
  };

  const size_t segments = chain.closed ? nsec : nsec - 1;
  std::vector<std::vector<SpineBulge>> out(segments);
  for (size_t i = 0; i < segments; ++i) {
    const double q0 = sectionAt[i];
    double q1 = sectionAt[(i + 1) % nsec];
    // Only a ring's closing cell runs off the end of the parameter. Anywhere
    // else two sections that do not advance are a ramp doubling back on a
    // station, and reading that as a wrap would hand the cell the whole crease.
    if (chain.closed && i + 1 == nsec && q1 <= q0) q1 += nsta;
    if (q1 - q0 < 1e-12) continue;

    const double p0 = rawParam(q0);
    double p1 = rawParam(q1);
    if (p1 <= p0) p1 += nraw;
    const Vector3d A = stationPoint(q0);
    const Vector3d B = stationPoint(q1);

    for (double j = std::floor(p0) + 1.0; j < p1 - 1e-9; j += 1.0) {
      if (j <= p0 + 1e-9) continue;
      double qj = chainParam(j >= nraw ? j - nraw : j);
      if (qj < 0.0) continue;
      if (qj < q0 - 1e-9) qj += nsta;
      const double s = (qj - q0) / (q1 - q0);
      if (!(s > 1e-9 && s < 1.0 - 1e-9)) continue;
      const size_t jj = static_cast<size_t>(j) % run.size();
      const Vector3d& P = m.pos[run[jj]];
      Vector3d off = P - (A + s * (B - A));
      if (off.norm() < 1e-12) continue;

      // Only ever grow the cell the way the tool is allowed to grow. A crease
      // vertex sits either side of its chord, and a cell reaching for one that
      // leans into the solid takes material no tool asked it to: on a coarse
      // cone that is enough to cut a fin clean off and leave it loose. The
      // component of the reach that points into the solid is dropped, and the
      // one that points out of it - the one the missed material is under - is
      // kept whole.
      const Vector3d away = wallsAway(m, adj, run, jj, chain.closed, concave);
      if (!away.isZero()) {
        const double into = off.dot(away);
        if (into < 0.0) off -= into * away;
        if (off.norm() < 1e-12) continue;
      }
      out[i].push_back({s, off});
    }
  }
  return out;
}

// Hull each consecutive pair of cross-sections along a chain, appending one cell
// per spine segment. A closed chain wraps, so its last station also pairs with
// its first. Degenerate segments (a zero-length spine step, or a section that
// collapsed) hull to nothing rather than to a bad solid and are dropped.
//
// A cell also carries the section at every crease vertex its chord passes inside
// of, slid off the chord onto that vertex. The hull is then of three or more
// coplanar-ended sections rather than two, and since it contains the two-section
// hull it can only ever cover more of the crease, never less. The end faces are
// untouched, so two neighbouring cells still meet on the one plane.
//
// `runs` restricts the work to the stretches the brushes selected, empty meaning
// all of it. Where a run starts or ends partway along a segment the two sections
// are interpolated to that parameter, which is exact rather than approximate:
// hulling two sections IS the linear interpolation of the cross-section between
// them, so slicing that cell at a parameter and hulling to the section at that
// parameter give the same solid. What the caller gets is a flat cap square to
// the spine, carrying the full cross-section.
template <typename Section, typename PointsOf, typename LerpOf>
void appendChainCells(const Chain& chain, const std::vector<Section>& sections,
                      const PointsOf& pointsOf, LerpOf lerpOf,
                      const std::vector<SpineInterval>& runs,
                      const std::vector<std::vector<SpineBulge>>& bulges,
                      std::vector<manifold::Manifold>& cells)
{
  const size_t n = sections.size();
  if (n < 2) return;

  const size_t segments = chain.closed ? n : n - 1;
  std::vector<SpineInterval> all;
  if (runs.empty()) all.emplace_back(0.0, static_cast<double>(segments));
  const std::vector<SpineInterval>& use = runs.empty() ? all : runs;

  for (const auto& [lo, hi] : use) {
    for (size_t i = 0; i < segments; ++i) {
      const double s0 = std::clamp(lo - static_cast<double>(i), 0.0, 1.0);
      const double s1 = std::clamp(hi - static_cast<double>(i), 0.0, 1.0);
      if (s1 - s0 <= 1e-12) continue;

      const Section& a = sections[i];
      const Section& b = sections[(i + 1) % n];
      if (!a.valid || !b.valid) continue;
      const Section head = s0 > 0.0 ? lerpOf(a, b, s0) : a;
      const Section tail = s1 < 1.0 ? lerpOf(a, b, s1) : b;
      if (!head.valid || !tail.valid) continue;

      std::vector<manifold::vec3> pts;
      for (const Section *section : {&head, &tail})
        for (const Vector3d& p : pointsOf(*section)) pts.emplace_back(p.x(), p.y(), p.z());

      if (i < bulges.size()) {
        for (const SpineBulge& bulge : bulges[i]) {
          if (bulge.s <= s0 + 1e-12 || bulge.s >= s1 - 1e-12) continue;
          const Section mid = lerpOf(a, b, bulge.s);
          if (!mid.valid) continue;
          for (const Vector3d& p : pointsOf(mid)) {
            const Vector3d q = p + bulge.off;
            pts.emplace_back(q.x(), q.y(), q.z());
          }
        }
      }

      manifold::Manifold cell = manifold::Manifold::Hull(pts);
      if (cell.IsEmpty()) continue;
      cells.push_back(std::move(cell));
    }
  }
}

// The centroid of a section's points, inside it for the convex sections these
// tools are built from.
template <typename Points>
Vector3d sectionAnchor(const Points& pts)
{
  Vector3d sum = Vector3d::Zero();
  for (const Vector3d& p : pts) sum += p;
  return pts.empty() ? sum : (sum / static_cast<double>(pts.size())).eval();
}

// The normal of the plane a section lies in, taken from the widest pair of
// directions in it rather than from the first two, which on a section whose
// stored order starts with two nearly collinear points would be noise.
template <typename Points>
Vector3d sectionNormal(const Points& pts, const Vector3d& anchor)
{
  if (pts.size() < 3) return Vector3d::Zero();
  const Vector3d e1 = (*pts.begin() - anchor).normalized();
  Vector3d normal = Vector3d::Zero();
  for (const Vector3d& p : pts) {
    const Vector3d cross = e1.cross(p - anchor);
    if (cross.norm() > normal.norm()) normal = cross;
  }
  return normal.norm() < 1e-18 ? Vector3d::Zero() : normal.normalized().eval();
}

// How far a point inside a section is from the section's own outline. The points
// are walked in the order they come round the anchor rather than the order they
// are stored in, since a canal section carries two points off its arc; one that
// falls inside the outline pulls the answer down, which is the safe direction.
template <typename Points>
double sectionClearance(const Points& pts, const Vector3d& anchor)
{
  const Vector3d normal = sectionNormal(pts, anchor);
  if (normal.isZero()) return 0.0;
  const Vector3d e1 = (*pts.begin() - anchor).normalized();
  const Vector3d e2 = normal.cross(e1);

  std::vector<std::pair<double, Vector3d>> around;
  around.reserve(pts.size());
  for (const Vector3d& p : pts) {
    const Vector3d d = p - anchor;
    around.emplace_back(std::atan2(d.dot(e2), d.dot(e1)), p);
  }
  std::sort(around.begin(), around.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  double least = std::numeric_limits<double>::infinity();
  for (size_t k = 0; k < around.size(); ++k) {
    const Vector3d& a = around[k].second;
    const Vector3d& b = around[(k + 1) % around.size()].second;
    const Vector3d ab = b - a;
    const double len2 = ab.squaredNorm();
    const double s = len2 > 0 ? std::clamp((anchor - a).dot(ab) / len2, 0.0, 1.0) : 0.0;
    least = std::min(least, (anchor - (a + s * ab)).norm());
  }
  return std::isfinite(least) ? least : 0.0;
}

// Cover the flat face two consecutive cells of a chain meet on, at every station
// they meet at.
//
// Cells are cut at stations and both of a rounded tool's unions are cut at the
// same ones, so a station hands the boolean a pair of coincident planes. Which
// way it resolves them is arithmetic: the seam is left standing as a flap of
// zero thickness in the finished solid, or cut through as a slit, and the answer
// moves with the tessellation, the size and where the model stands.
//
// The cover is the station's own section hulled with one point inside each
// neighbouring cell, which straddles the seam. It approximates nothing: the hull
// of a planar section with a point either side of its plane is the union of two
// cones, and each cone is the hull of a subset of one cell — the section is that
// cell's own end face and the apex a point of the axis it is hulled along. So it
// reaches the rim of the seam, which is the whole of what has to be covered and
// is what a ball inscribed in the section cannot do: a ball leaves the annulus
// at the rim, and the flap survives out there.
//
// `anchorOf` is a point inside the section, which is where the apexes are taken
// from: the two of them have to see each other through the section's interior
// for the two cones to meet.
template <typename Section, typename PointsOf, typename AnchorOf>
void appendSeamCovers(const Chain& chain, const std::vector<Section>& sections,
                      const PointsOf& pointsOf, const AnchorOf& anchorOf,
                      const std::vector<SpineInterval>& runs,
                      std::vector<manifold::Manifold>& cells)
{
  const size_t n = sections.size();
  if (n < 3) return;

  for (size_t i = 0; i < n; ++i) {
    const Section& sec = sections[i];
    const Section& prev = sections[(i + n - 1) % n];
    const Section& next = sections[(i + 1) % n];
    if (!sec.valid || !prev.valid || !next.valid) continue;

    // How much of a segment of cell there is to reach into on each side. A brush
    // ends its run partway along a segment, and an apex past that is outside the
    // cell it was meant to be inside.
    const double at = static_cast<double>(i);
    double roomBack = 0.0, roomFwd = 0.0;
    if (runs.empty()) {
      if (!chain.closed && (i == 0 || i + 1 == n)) continue;
      roomBack = roomFwd = 1.0;
    } else {
      for (const SpineInterval& iv : runs)
        if (at > iv.first + 1e-12 && at < iv.second - 1e-12) {
          roomBack = std::min(1.0, at - iv.first);
          roomFwd = std::min(1.0, iv.second - at);
          break;
        }
      if (!(roomBack > 0.0 && roomFwd > 0.0)) continue;
    }

    // Only where the two cells meet at an angle. Where three consecutive
    // sections lie in a line they are two halves of one prism: the seam has the
    // same outline on both sides, nothing grazes it, and a cover over it is one
    // more solid for the boolean to reconcile for nothing.
    {
      const auto& here = pointsOf(sec);
      const auto& before = pointsOf(prev);
      const auto& after = pointsOf(next);
      const size_t count = std::min(here.size(), std::min(before.size(), after.size()));
      double off = 0.0, span = 0.0;
      for (size_t k = 0; k < count; ++k) {
        off = std::max(off, (0.5 * (before[k] + after[k]) - here[k]).norm());
        span = std::max(span, (after[k] - before[k]).norm());
      }
      if (off <= 1e-9 * span) continue;
    }

    // How far along the anchors either way the two apexes are taken. An apex may
    // travel as far as the neighbouring section's own anchor without leaving the
    // cell, since the whole of that segment is in it. What bounds it instead is
    // where the segment between the two apexes crosses the section's plane: that
    // point has to be inside the section, or the hull bridges round the outside
    // of it and the cover is no longer made of the two cells' own material. The
    // crossing point leaves the anchor linearly with the reach — scaling both
    // apexes about the anchor scales the whole figure — so the reach that keeps
    // it inside is arithmetic rather than a guess. Half the anchor's clearance of
    // the section's boundary, which leaves the bridge as much room again.
    const Vector3d here = anchorOf(sec);
    const Vector3d back = anchorOf(prev) - here;
    const Vector3d fwd = anchorOf(next) - here;

    const Vector3d normal = sectionNormal(pointsOf(sec), here);
    if (normal.isZero()) continue;
    const double behind = normal.dot(back), ahead = normal.dot(fwd);
    if (!(behind * ahead < 0.0)) continue;  // both neighbours the same side of the seam

    double reach = 0.5;
    {
      const double along = behind / (behind - ahead);
      const double drift = (back + along * (fwd - back)).norm();
      const double clearance = sectionClearance(pointsOf(sec), here);
      if (drift > 0.0) reach = std::min(reach, 0.5 * clearance / drift);
    }
    reach = std::min(reach, std::min(roomBack, roomFwd));
    if (!(reach > 0.0)) continue;

    const Vector3d qBack = here + reach * back;
    const Vector3d qFwd = here + reach * fwd;

    std::vector<manifold::vec3> pts;
    for (const Vector3d& p : pointsOf(sec)) pts.emplace_back(p.x(), p.y(), p.z());
    pts.emplace_back(qBack.x(), qBack.y(), qBack.z());
    pts.emplace_back(qFwd.x(), qFwd.y(), qFwd.z());

    manifold::Manifold cover = manifold::Manifold::Hull(pts);
    if (!cover.IsEmpty()) cells.push_back(std::move(cover));
  }
}

// The canal has a round cap, so ending it where the wedge ends would let that
// hemisphere bulge back through the cut plane and scoop a dish out of the flat
// end face. Run it a segment past instead, in both directions: past the wedge's
// end the canal has nothing to subtract from and overhanging is free.
std::vector<SpineInterval> overhang(const std::vector<SpineInterval>& runs, size_t segments)
{
  if (runs.empty()) return runs;
  std::vector<SpineInterval> out;
  for (const SpineInterval& iv : runs) {
    const SpineInterval wide{std::max(0.0, iv.first - 1.0),
                             std::min(static_cast<double>(segments), iv.second + 1.0)};
    if (!out.empty() && wide.first <= out.back().second) {
      out.back().second = std::max(out.back().second, wide.second);
      continue;
    }
    out.push_back(wide);
  }
  return out;
}

// Whether one interval of a chain's selection contains a stretch of it.
bool intervalCovers(const SpineInterval& iv, double lo, double hi)
{
  return iv.first <= lo + 1e-9 && iv.second >= hi - 1e-9;
}

// Whether a chain's selection merely arrives at its end station.
bool endTouched(const Chain& chain, bool front)
{
  if (chain.keep.empty()) return true;
  const size_t n = chain.verts.size();
  if (n < 2) return false;
  const double vertex = front ? 0.0 : static_cast<double>(n - 1);
  for (const SpineInterval& iv : chain.keep)
    if (intervalCovers(iv, vertex, vertex)) return true;
  return false;
}

// The stretch a corner cell at one end of a chain occupies, in that chain's
// parameter: `reach` of crease measured back from the end vertex, walked station
// by station because segments differ in length. A chain shorter than the reach
// gives the whole of itself.
SpineInterval endWindow(const MergedMesh& m, const Chain& chain, bool front, double reach)
{
  const size_t n = chain.verts.size();
  const double vertex = front ? 0.0 : static_cast<double>(n - 1);
  const double step = front ? 1.0 : -1.0;
  double param = vertex;
  double left = reach;
  for (size_t k = 0; k + 1 < n && left > 0.0; ++k) {
    const size_t i = front ? k : n - 1 - k;
    const size_t j = front ? k + 1 : n - 2 - k;
    const double seg =
      (chain.point(m.pos, static_cast<int>(j)) - chain.point(m.pos, static_cast<int>(i))).norm();
    if (seg >= left) {
      param += step * (left / seg);
      break;
    }
    left -= seg;
    param = static_cast<double>(j);
  }
  return {std::min(vertex, param), std::max(vertex, param)};
}

// Whether a chain's selection covers the whole stretch a corner cell would take
// from it: `reach` of crease measured back from the end vertex, which is where
// the bead is truncated and the corner takes over.
//
// A junction is built only where every chain arriving there covers it, and why
// touching the vertex is not enough is that the cell is a fixed size — it is
// hulled from the seated ball and the sections the beads stop at, and there is no
// perpendicular to clip it against in three directions at once. So a brush that
// reaches a corner by a fraction of `reach` still gets the whole of it, which is
// the brush contract broken by however much was missing, while along an edge the
// same brush is honoured to the micron. The decision is therefore binary at
// `reach`: cover it and get a corner, fall inside it and get none — and the
// stretch that fell inside is dropped with the corner rather than built as a stub
// meeting nothing, which is what dropUncoveredCorners does to it.
//
// `reach` is the tool's own size, which is what every caller passes: r for a
// rounded tool, and not the trigonometric setback r * tan(phi/2) the
// cross-section works in. The two agree only at a right angle.
//
// A crease that was never selected is a different thing and does not stop a
// corner. Two beads that were built still meet at the vertex whether the third
// crease was refused for size, turned the other way or fell below the threshold,
// and they cusp there if nothing closes it. What this rule protects is the corner
// the caller reached for and did not cover.
bool endAnchored(const MergedMesh& m, const Chain& chain, bool front, double reach)
{
  if (chain.keep.empty()) return true;
  if (chain.verts.size() < 2) return false;

  const SpineInterval window = endWindow(m, chain, front, reach);
  for (const SpineInterval& iv : chain.keep)
    if (intervalCovers(iv, window.first, window.second)) return true;
  return false;
}

// The selection, rewritten from chain-parameter space into the section-index
// space the cells are built in. `at` is where each section sits along the chain,
// and it is increasing, so this is the piecewise-linear inverse of it. An
// identity `at` is left alone rather than walked. A run that maps to a single
// point is dropped, which can empty a non-empty selection — the caller has to
// tell that apart from the empty list that means no brush at all.
std::vector<SpineInterval> toSectionSpace(const std::vector<SpineInterval>& runs,
                                          const std::vector<double>& at, bool closed)
{
  if (runs.empty() || at.size() < 2) return runs;
  bool identity = true;
  for (size_t i = 0; i < at.size() && identity; ++i) identity = at[i] == static_cast<double>(i);
  // A closed chain has no ends to truncate or run out at, so its sections never
  // leave their stations — and its parameter runs one past the last of them,
  // which the open-chain clamp below would swallow.
  if (identity || closed) return runs;

  auto index = [&](double p) {
    if (p <= at.front()) return 0.0;
    if (p >= at.back()) return static_cast<double>(at.size() - 1);
    const auto it = std::upper_bound(at.begin(), at.end(), p);
    const size_t i = static_cast<size_t>(it - at.begin()) - 1;
    const double span = at[i + 1] - at[i];
    return static_cast<double>(i) + (span > 0 ? (p - at[i]) / span : 0.0);
  };

  std::vector<SpineInterval> out;
  for (const SpineInterval& iv : runs) {
    const SpineInterval mapped{index(iv.first), index(iv.second)};
    if (mapped.second - mapped.first > 1e-12) out.push_back(mapped);
  }
  return out;
}

}  // namespace

std::vector<WedgeSection> wedgeSections(const MergedMesh& m,
                                        const std::map<EdgeKey, std::vector<int>>& adj,
                                        const Chain& chain, double t, bool concave,
                                        double thresholdDeg)
{
  const std::vector<StationNormals> stations = chainNormals(m, adj, chain);

  // The fixed part of how far past each wall the tool reaches — the whole of it
  // on a flat wall. Relative to the setback so it scales with the feature, with a
  // floor so a degenerate t still separates the faces by more than the boolean
  // kernel's own tolerance.
  const double eps = std::max(1e-3 * std::abs(t), 1e-9);

  // A concave tool is unioned into the material, so its overshoot points inward,
  // against the outward wall normals; a convex tool is subtracted and overshoots
  // into the air. Setback follows the same sign: a concave tool spans the
  // reentrant quadrant, a convex one cuts into the solid.
  const double dir = concave ? 1.0 : -1.0;

  std::vector<WedgeSection> out(stations.size());
  for (size_t i = 0; i < stations.size(); ++i) {
    const StationNormals& s = stations[i];
    if (!s.valid) continue;

    // In-wall directions by Gram-Schmidt: uA is nB with its nA component removed,
    // so it lies in wall A and points across the crease. Degenerate when the
    // walls are parallel (a flat seam or a fold back on itself), where there is
    // no crease to cut.
    const double c = std::clamp(s.nA.dot(s.nB), -1.0, 1.0);
    Vector3d uA = s.nB - c * s.nA;
    Vector3d uB = s.nA - c * s.nB;
    if (uA.norm() < 1e-9 || uB.norm() < 1e-9) continue;
    uA.normalize();
    uB.normalize();

    Vector3d bis = s.nA + s.nB;
    if (bis.norm() < 1e-9) continue;
    bis.normalize();

    const Vector3d TA = s.v + dir * t * uA;
    const Vector3d TB = s.v + dir * t * uB;

    WedgeSection w;
    w.p = pentagonSection(s.v, s.nA, s.nB, bis, TA, TB, dir,
                          wallOvershoot(m, adj, s.v, s.nA, s.triA, TA, eps, thresholdDeg),
                          wallOvershoot(m, adj, s.v, s.nB, s.triB, TB, eps, thresholdDeg));
    w.valid = true;
    out[i] = w;
  }
  return out;
}

manifold::Manifold buildWedgeSolid(const MergedMesh& m,
                                   const std::map<EdgeKey, std::vector<int>>& adj,
                                   const std::vector<Chain>& chains, double t, bool concave,
                                   double thresholdDeg)
{
  if (!(t > 0)) return {};

  // The pentagon's corners all move linearly along a straight spine segment, so
  // the section partway along one is the section's points partway along theirs.
  auto lerpWedge = [](const WedgeSection& a, const WedgeSection& b, double s) {
    WedgeSection out;
    if (!a.valid || !b.valid) return out;
    for (size_t k = 0; k < a.p.size(); ++k) out.p[k] = a.p[k] + s * (b.p[k] - a.p[k]);
    out.valid = true;
    return out;
  };

  auto pointsOf = [](const WedgeSection& s) -> const std::array<Vector3d, 5>& { return s.p; };

  std::vector<manifold::Manifold> cells;
  for (const Chain& chain : chains) {
    const std::vector<WedgeSection> sections =
      wedgeSections(m, adj, chain, t, concave, thresholdDeg);
    std::vector<double> at(sections.size());
    for (size_t i = 0; i < at.size(); ++i) at[i] = static_cast<double>(i);
    appendChainCells(chain, sections, pointsOf, lerpWedge, chain.keep,
                     chainBulges(m, adj, chain, at, concave), cells);
    appendSeamCovers(chain, sections, pointsOf,
                     [](const WedgeSection& s) { return sectionAnchor(s.p); }, chain.keep, cells);
  }

  return unionCells(cells);
}

namespace {

// The two cross-sections at one point of a crease, given the walls there and the
// radius to use at that point. Taking the radius as an argument rather than
// reading one for the whole chain is what lets a spine ramp its bead down to
// nothing where a corner cannot be built.
RoundSection makeRoundSection(const MergedMesh& m,
                              const std::map<EdgeKey, std::vector<int>>& adj, const SpineFrame& f,
                              double r, bool concave, int segs, double eps, double thresholdDeg)
{
  const Vector3d& v = f.v;
  const Vector3d& nA = f.nA;
  const Vector3d& nB = f.nB;
  RoundSection s;
  if (!(r > 0)) return s;

  const double dir = concave ? 1.0 : -1.0;
  const double cosPhi = std::clamp(nA.dot(nB), -1.0, 1.0);
  const double phi = std::acos(cosPhi);

  Vector3d bis = nA + nB;
  if (phi > 179.0 * M_PI / 180.0 || bis.norm() < 1e-9) return s;
  bis.normalize();

  // The plane of the section is the one spanned by the two wall normals: C, v,
  // TA and TB all lie in it by construction, so the arc meets each wall
  // tangentially there whatever the spine does between stations. Taking it from
  // the normals rather than from the spine direction is what keeps that true
  // around a bend, where the two disagree.
  Vector3d e = nA.cross(nB);
  if (e.norm() < 1e-12) return s;
  e.normalize();
  const Vector3d e1 = nA;  // unit, and perpendicular to e
  const Vector3d e2 = e.cross(e1);

  const Vector3d C = v + dir * (r / std::cos(phi / 2.0)) * bis;
  const Vector3d TA = C - dir * r * nA;
  const Vector3d TB = C - dir * r * nB;

  const double epsA = wallOvershoot(m, adj, v, nA, f.triA, TA, eps, thresholdDeg);
  const double epsB = wallOvershoot(m, adj, v, nB, f.triB, TB, eps, thresholdDeg);

  s.w = pentagonSection(v, nA, nB, bis, TA, TB, dir, epsA, epsB);
  s.eps = std::max(epsA, epsB);
  s.v = v;
  s.C = C;
  s.u.reserve(segs + 2);
  for (int k = 0; k < segs; ++k) {
    const double a = 2.0 * M_PI * k / segs;
    s.u.push_back(C + r * (std::cos(a) * e1 + std::sin(a) * e2));
  }
  // Two more, one per wall, in the tangency directions and further out than
  // anything the subtraction has to cut through. Without them the arc only
  // kisses each wall while the wedge reaches eps past it, so what the
  // subtraction leaves is a strip of the wedge's own overshoot — eps thick, as
  // long as the crease, and running out to nothing where the arc curves away
  // from the wall at either end of it. That strip is the sliver, and no amount
  // of arc segments removes it: it is the gap between a tangent and its tangent
  // plane, so refining the tangent only makes it thinner. Taking the arc past
  // the wall instead has it cross the wedge's wall face at a real angle and the
  // strip never exists.
  //
  // The arc itself is untouched — these are two extra points for the hull to
  // reach, not a larger ball — so the blend still meets each wall where a ball
  // of exactly r touches it, to within the overshoot the tool already carries
  // there. The distance is the top of the ladder every piece of the tool sits
  // on: the wedges stand eps past each wall, the corner cells further, and what
  // is subtracted further still, so that a cut always crosses a face and never
  // arrives along it.
  s.u.push_back(TA - dir * 2.0 * epsA * nA);
  s.u.push_back(TB - dir * 2.0 * epsB * nB);
  s.valid = true;
  return s;
}

}  // namespace

std::vector<RoundSection> roundSections(const MergedMesh& m,
                                        const std::map<EdgeKey, std::vector<int>>& adj,
                                        const Chain& chain, double r, bool concave,
                                        int arcSegments, double thresholdDeg)
{
  const std::vector<SpineFrame> frames = spineFrames(m, adj, chain, r, concave);

  const double eps = std::max(1e-3 * std::abs(r), 1e-9);
  // Three points is the coarsest thing that still bounds an area; a radius too
  // small for the discretizer to have an opinion about lands here.
  const int segs = std::max(arcSegments, 3);

  std::vector<RoundSection> out(frames.size());
  for (size_t i = 0; i < frames.size(); ++i) {
    const SpineFrame& f = frames[i];
    if (!f.valid) continue;
    out[i] = makeRoundSection(m, adj, f, r, concave, segs, eps, thresholdDeg);
  }
  return out;
}

std::vector<int> dropUncoveredCorners(const MergedMesh& m, std::vector<Chain>& chains,
                                      const std::vector<Chain>& candidates, double r,
                                      std::set<int> *uncoveredOut)
{
  // How many creases the corner has, asked of the selection before the brush.
  std::map<int, int> arms;
  for (const Chain& chain : candidates) {
    if (chain.closed || chain.verts.size() < 2) continue;
    ++arms[chain.verts.front()];
    ++arms[chain.verts.back()];
  }

  std::map<int, int> touching, covering;
  for (const Chain& chain : chains) {
    if (chain.closed || chain.verts.size() < 2) continue;
    for (const bool front : {true, false}) {
      if (!endTouched(chain, front)) continue;
      const int v = front ? chain.verts.front() : chain.verts.back();
      ++touching[v];
      if (endAnchored(m, chain, front, r)) ++covering[v];
    }
  }

  // Three creases is what makes a corner, and covering every one of them for `r`
  // back from the vertex is what lets it be built; a vertex over the first and
  // short of the second is a corner that cannot be had, and everything cut short
  // at it goes — the stubs here, and the cell itself through the set handed back.
  //
  // Both counts have to be of the same arms or the rule inverts. Measured against
  // the arms that survived the brush, a corner appeared as the brush shrank: an
  // arm covered by less than the debounce leaves the selection outright, so two
  // arms remained where three arrived, the vertex was never marked, and the cell
  // came back at full size. Less of the brush bought more material, and the
  // switch sat at a hundredth of the size rather than at the size.
  //
  // A vertex fewer than three creases arrive at is not this rule's business: two
  // that meet because the third was never a candidate — refused for size, turned
  // the other way, too shallow — is a corner chainJunctions closes on its own,
  // and no brush asked otherwise.
  // Arriving at all is what makes it this rule's business rather than the
  // model's. A corner the brush is nowhere near has no end covering its vertex,
  // so nothing is dropped there and no cell was going to be built there either —
  // marking it would be inert, and it would stop the set meaning what its name
  // says.
  std::set<int> uncovered;
  for (const auto& [v, count] : arms)
    if (count >= 3 && touching[v] > 0 && covering[v] < count) uncovered.insert(v);
  if (uncoveredOut != nullptr) *uncoveredOut = uncovered;
  if (uncovered.empty()) return {};

  // Of those, the ones worth telling the caller about are where nothing at all
  // was covered: the brush was drawn around that corner and gets nothing there,
  // which is the one place a brush is answered with silence. Where some crease
  // through the vertex was covered, the brush was aimed along it and the stubs
  // going with the corner are the neighbours it inevitably clipped — dropping
  // those is what makes "this edge and no other" expressible, the count of edges
  // taken already reports it, and a warning would fire on every use of it.
  std::vector<int> out;
  for (const int v : uncovered)
    if (covering[v] == 0 && touching[v] >= 3) out.push_back(v);

  // Every stretch running into an uncovered corner, cut short of the cell it
  // would have met, goes with it. A stretch is short at one end at most — one
  // covering the whole of its chain covers both ends by definition — so one pass
  // settles this, and the counts above stay the counts the decision was made on.
  std::vector<Chain> keeping;
  for (Chain& chain : chains) {
    const size_t n = chain.verts.size();
    if (!chain.keep.empty() && !chain.closed && n >= 2) {
      std::vector<SpineInterval> kept;
      for (const SpineInterval& iv : chain.keep) {
        bool drop = false;
        for (const bool front : {true, false}) {
          const int v = front ? chain.verts.front() : chain.verts.back();
          if (uncovered.count(v) == 0) continue;
          const double vertex = front ? 0.0 : static_cast<double>(n - 1);
          const SpineInterval window = endWindow(m, chain, front, r);
          if (intervalCovers(iv, vertex, vertex) &&
              !intervalCovers(iv, window.first, window.second))
            drop = true;
        }
        if (!drop) kept.push_back(iv);
      }
      if (kept.empty()) continue;  // nothing of this chain survives
      chain.keep = std::move(kept);
    }
    keeping.push_back(std::move(chain));
  }
  chains = std::move(keeping);
  return out;
}

// Does a crease of the model leave this vertex unfilleted?
//
// A corner ball rounds across everything that arrives at a vertex. Where a sharp
// edge of the model continues out of that vertex, there is nothing a ball can do
// that lines up with it — it rounds the edge's own start away — so the two beads
// that do arrive have to meet each other in a seam along that edge instead. Where
// nothing sharp leaves, there is nothing to line up with and the ball is right.
//
// Why an edge counts is deliberately not asked. A crease the brush excluded, one
// refused for size, and one turning the other way all leave the same sharp edge
// in the output, and the blend owes that edge the same seam.
//
// What counts as a crease of the model is `isFeatureAngle` and nothing else — the
// same threshold the selection itself is made on. That is what keeps a
// tessellated wall out of this: the seams between the facets of a cylinder turn
// by a fraction of the threshold, so they are not creases here any more than they
// are creases to the selection, and two bead segments meeting at such a seam
// still get their ball. Reading curvature off the tessellation instead would put
// a seam at every facet boundary.
std::set<EdgeKey> filletedEdges(const std::vector<Chain>& chains)
{
  // A station is a mesh vertex only when it was put there by the crease walk. A
  // chain whose stations have been placed along the spine instead — at equal arc
  // length, say — carries -1 wherever a station falls between two mesh vertices,
  // and the pair either side of one is not a mesh edge at all. Reading such a
  // pair as an EdgeKey would index the mesh at -1; skipping it is the only
  // honest answer this function can give from `verts` alone. Where a chain can
  // carry interpolated stations, what this wants is the mesh run underneath it,
  // and the caller has to hand that over rather than the station list.
  // The mesh run underneath is exactly what `rawRun()` is, and every chain
  // carries it, so ask that rather than skipping the pairs that cannot be read.
  // Skipping is safe but not right: it drops the filleted edges either side of
  // an interpolated station, and a crease read as unfilleted when it is in fact
  // filleted makes a seam vertex out of one that is not.
  std::set<EdgeKey> out;
  for (const Chain& c : chains) {
    const std::vector<int>& run = c.rawRun();
    for (size_t i = 0; i + 1 < run.size(); ++i) {
      const int a = run[i], b = run[i + 1];
      if (a < 0 || b < 0) continue;
      out.insert(EdgeKey{std::min(a, b), std::max(a, b)});
    }
  }
  return out;
}

// Does this chain arrive at the given end running straight?
//
// The seam rule replaces a seated ball — a construction solved against whatever
// walls it finds — with a pair of beads carried PAST the vertex along the
// straight line their last segment lies on. That line is the crease's own
// continuation only where the crease arrives straight. Where it arrives on a
// curve, the straight line is a chord produced past its second end, and it leaves
// the circle on the outside: the top of the bead's cross-section is tangent to
// the curved wall the crease rides, so carrying it along that line lays it a hair
// OUTSIDE the wall, and the tangency becomes a sliver of surface resting on the
// wall at a hair of an angle. That is the very degeneracy the overrun exists to
// remove, put back a little further along — and it is what a bead run past the
// foot of a cylinder does. A seated ball has no such problem, so a vertex a bead
// arrives at on a curve is handed back to it.
//
// One segment is straight with nothing to check: a crease with no station between
// its ends is a straight line, and the line its segment lies on is itself.
bool arrivesStraight(const MergedMesh& m, const Chain& c, bool front)
{
  // Of the crease, not of the stations: a resampled chain carries -1 where a
  // station falls between two mesh vertices, and m.pos[-1] is a read off the
  // front of the array. The curve whose chord is produced past its end is the
  // mesh's own, so the crease run is also the right question.
  const std::vector<int>& run = c.rawRun();
  const size_t n = run.size();
  if (n < 3) return true;
  const Vector3d end = m.pos[front ? run[0] : run[n - 1]];
  const Vector3d mid = m.pos[front ? run[1] : run[n - 2]];
  const Vector3d far = m.pos[front ? run[2] : run[n - 3]];
  const Vector3d a = end - mid, b = mid - far;
  const double la = a.norm(), lb = b.norm();
  if (la < 1e-12 || lb < 1e-12) return true;
  return a.dot(b) / (la * lb) > 1.0 - 1e-9;
}

bool creaseLeavesUnfilleted(const MergedMesh& m,
                            const std::map<EdgeKey, std::vector<int>>& adj,
                            const std::set<EdgeKey>& filleted, double thresholdDeg, int v)
{
  for (const auto& [key, ts] : adj) {
    if (key.first != v && key.second != v) continue;
    if (ts.size() != 2) continue;
    const EdgeClass ec = classifyEdge(m, key, m.tris[ts[0]], m.tris[ts[1]]);
    if (!isFeatureAngle(ec.dihedralDeg, thresholdDeg)) continue;  // tessellation seam
    if (filleted.count(key) == 0) return true;
  }
  return false;
}

// How far a bead may run past a seam vertex before it runs out of the part it is
// running through.
//
// The overrun past a seam vertex is free only where the part goes on past the
// vertex: the bead continues into the wall the unfilleted crease runs along,
// which for a concave blend is solid the caller unions onto, and for a convex one
// is air outside the part that the caller's cut never has to reach. None of that
// holds for a wall thinner than the overrun, for one that turns away inside it,
// or for one that ends inside it — there the bead crosses a face of the part
// nothing was blending and stands proud of it.
//
// So the swept end of the bead is measured against the mesh: every corner of the
// end profile is carried along the overrun direction, and the first face it
// crosses is where the room runs out. Faces of the vertex's own fan are not
// crossings — they are the walls the bead is tangent to and its profile starts
// on — and neither are faces the overrun merely runs along, since a bead sliding
// tangentially past the facets of a curved wall never leaves it.
//
// Only a face the run pushes the bead OUT through can bound it, which is what the
// outward normal is read for: a face the run carries the bead INTO is solid
// closing over it, not a face it will stand proud of.
//
// The crossing counts whether it lies ahead of the profile corner or behind it.
// Behind means that corner started on the far side of the face already: at an
// opening angle under a right angle the corner of the end profile sits r*cos(θ)
// into the neighbouring wall, so a wall thinner than that has the bead poking out
// of it before the overrun begins. Carrying such a corner further along the run
// only takes it further out, so it has no room at all — and reading forward only
// missed that, left the overrun unbounded, and put the bead a quarter of a
// millimetre proud of a wall one and a half millimetres thick.
//
// `reach` is how far to look, in BOTH directions, and is also the answer where
// nothing was found. Forward it is the point past which more room stops making a
// difference. Backward it is how far out a face may already have been breached
// and still be one this overrun is about to make worse: a bead standing more than
// its whole overrun proud of a wall is a radius that does not fit that wall,
// which is a complaint about the radius, and letting it withhold the overrun
// would only take the seam repair away from the models that need it most.
double seamRoom(const MergedMesh& m, int vert, const std::vector<Vector3d>& from,
                const Vector3d& dir, double reach)
{
  // A face the overrun is more parallel to than this is one it runs along rather
  // than through. The facets of a tessellated wall are the case that matters:
  // consecutive facets turn by a fraction of a degree and their planes lie a
  // hair off the ray, so without this a curved wall would read as no room at all.
  constexpr double kMinCross = 0.1;
  constexpr double kOnFace = 1e-6;

  // The box spans the run both ways, since a face already breached lies behind
  // the profile corner that breached it.
  Vector3d lo = from.front(), hi = from.front();
  for (const Vector3d& p : from) {
    lo = lo.cwiseMin(p - reach * dir).cwiseMin(p + reach * dir);
    hi = hi.cwiseMax(p - reach * dir).cwiseMax(p + reach * dir);
  }

  double room = reach;
  for (const auto& tri : m.tris) {
    if (tri.v[0] == vert || tri.v[1] == vert || tri.v[2] == vert) continue;
    const Vector3d& a = m.pos[tri.v[0]];
    const Vector3d& b = m.pos[tri.v[1]];
    const Vector3d& c = m.pos[tri.v[2]];
    if (a.cwiseMin(b).cwiseMin(c).x() > hi.x() || a.cwiseMax(b).cwiseMax(c).x() < lo.x()) continue;
    if (a.cwiseMin(b).cwiseMin(c).y() > hi.y() || a.cwiseMax(b).cwiseMax(c).y() < lo.y()) continue;
    if (a.cwiseMin(b).cwiseMin(c).z() > hi.z() || a.cwiseMax(b).cwiseMax(c).z() < lo.z()) continue;

    const Vector3d e1 = b - a, e2 = c - a;
    const Vector3d n = e1.cross(e2);
    const double area2 = n.norm();
    if (area2 < 1e-18) continue;
    // Signed, not absolute: the run has to be going the way the face looks for
    // crossing it to leave the bead outside.
    if (n.dot(dir) / area2 < kMinCross) continue;
    // A face the vertex itself lies in is one of its own walls however it was
    // triangulated, so a triangle of it that does not carry the vertex is still
    // not something the bead crosses.
    if (std::abs(n.dot(m.pos[vert] - a) / area2) < kOnFace * std::max(1.0, reach)) continue;

    for (const Vector3d& s : from) {
      const Vector3d pv = dir.cross(e2);
      const double det = e1.dot(pv);
      if (std::abs(det) < 1e-18) continue;
      const double inv = 1.0 / det;
      const Vector3d tv = s - a;
      const double u = tv.dot(pv) * inv;
      if (u < 0.0 || u > 1.0) continue;
      const Vector3d qv = tv.cross(e1);
      const double v = dir.dot(qv) * inv;
      if (v < 0.0 || u + v > 1.0) continue;
      const double t = e2.dot(qv) * inv;
      if (t < -reach || t > room) continue;
      room = std::max(0.0, t);
    }
  }
  return room;
}

std::vector<Junction> chainJunctions(const MergedMesh& m,
                                     const std::map<EdgeKey, std::vector<int>>& adj,
                                     const std::vector<Chain>& chains, double r, bool concave,
                                     const std::set<int>& noCorner)
{
  // Chains are cut at every vertex whose crease degree is not two, so the number
  // of chain ends landing on a vertex is that degree. A closed ring has no ends
  // and never contributes.
  //
  // An end the brushes cut short of r does not count: the corner cell is the full
  // seated ball whatever is selected, so building one for a brush that covers part
  // of the stretch it occupies puts material outside what was asked for. That is
  // the whole of `noCorner`, which the brush pass hands over — the same vertices
  // it drops the arriving stubs at, so that the cell and the stubs go together
  // rather than one of them surviving the other.
  //
  // Two ends is enough anywhere else. Three is the valence of a vertex where
  // three creases meet, but only some of them need be selected: one refused for
  // size, or turning the other way, or too shallow, leaves two ends on a vertex
  // that is still a corner of the model. The two beads that were built arrive
  // there together, tangent to the wall they share, and their footprints on it
  // cross about a radius out — where the two surfaces meet at no angle at all.
  // That is a cusp, which a boolean can only resolve into a flap, and measured
  // over the opening angle of a plain two-wall corner it is what happens at 60,
  // 75, 105 and 120 degrees. Ninety comes back clean, because there the two beads
  // are mirror images and their intersection lands on the symmetry plane; that is
  // a property of the mesh and not of the shape, so it is not a rule. The corner
  // cell fills the valley, and it is solved against every wall at the vertex
  // either way, so the wall of the crease that is missing is already one of the
  // ball's constraints.
  std::map<int, std::vector<int>> ends;  // vertex -> the next station along each end
  for (const Chain& chain : chains) {
    if (chain.closed || chain.verts.size() < 2) continue;
    if (endAnchored(m, chain, /*front=*/true, r))
      ends[chain.verts.front()].push_back(chain.verts[1]);
    if (endAnchored(m, chain, /*front=*/false, r))
      ends[chain.verts.back()].push_back(chain.verts[chain.verts.size() - 2]);
  }

  // Every triangle that touches a vertex, so a junction can be asked about the
  // walls around it rather than only about the ones its own creases ride.
  std::map<int, std::vector<int>> trisAt;
  for (size_t t = 0; t < m.tris.size(); ++t)
    for (const int v : m.tris[t].v) trisAt[v].push_back(static_cast<int>(t));

  const double dir = concave ? 1.0 : -1.0;

  std::vector<Junction> out;
  for (const auto& [v, nbrs] : ends) {
    if (nbrs.size() < 2 || noCorner.count(v) != 0) continue;

    Junction j;
    j.vert = v;
    // Every wall meeting at the vertex constrains the ball, not just the walls
    // of the creases this tool selected. Where a convex edge arrives at a
    // concave corner, its far face is the one a centre solved from the concave
    // walls alone would end up buried in, and nothing about that crease's own
    // sign makes the face any less solid.
    const auto tit = trisAt.find(v);
    if (tit != trisAt.end())
      for (const int t : tit->second) {
        const Vector3d& n = m.tris[t].normal;
        bool seen = false;
        for (const Vector3d& q : j.faceNormals)
          if (q.dot(n) > 1.0 - 1e-9) { seen = true; break; }
        if (!seen) j.faceNormals.push_back(n);
      }

    // Every wall passes through the vertex, so a ball tangent to three of them
    // solves n_i . (P - v) = +-r. Take every triple: at three walls that is the
    // single corner, and at more it enumerates the candidates, of which only the
    // ones clear of all the remaining walls are positions the ball can actually
    // reach.
    const size_t k = j.faceNormals.size();
    const double slack = 1e-6 * r;
    for (size_t a = 0; a + 2 < k; ++a)
      for (size_t b = a + 1; b + 1 < k; ++b)
        for (size_t c = b + 1; c < k; ++c) {
          Matrix3d M;
          M.row(0) = j.faceNormals[a].transpose();
          M.row(1) = j.faceNormals[b].transpose();
          M.row(2) = j.faceNormals[c].transpose();

          // The rows are unit vectors, so the determinant is the volume of the
          // parallelepiped they span: it goes to zero exactly as the three walls
          // stop pinning a point down.
          if (std::abs(M.determinant()) <= 1e-6) continue;

          const Vector3d P = m.pos[v] + M.inverse() * (dir * r * Vector3d::Ones());
          // Nominal success is not enough: nearly-coplanar walls clear the
          // determinant test and still put the centre absurdly far off.
          if (!P.allFinite() || (P - m.pos[v]).norm() > 10.0 * r) continue;

          bool reachable = true;
          for (const Vector3d& n : j.faceNormals)
            if (dir * n.dot(P - m.pos[v]) < r - slack) { reachable = false; break; }
          if (!reachable) continue;

          // A symmetric junction solves to the same centre from every triple.
          bool seen = false;
          for (const Vector3d& q : j.ballCentres)
            if ((q - P).norm() <= slack) { seen = true; break; }
          if (!seen) j.ballCentres.push_back(P);
        }

    out.push_back(std::move(j));
  }
  return out;
}

namespace {

// How far along the last spine segment the ball may still travel: the first
// point at which it touches a wall it is not riding. Past there it would cut
// into material, so that is where the spine stops and the corner cell takes
// over. Phrased as a distance to every wall at the junction rather than as a
// solve, it needs no special case per valence — the walls the spine does ride
// sit at exactly r all along it and never bind, and at degree three every
// incident spine stops at the same point. Returned as a fraction of the segment
// from `C0` toward `C1`; 1 means nothing binds.
double truncationParam(const Junction& j, const Vector3d& vj, const Vector3d& C0,
                       const Vector3d& C1, double r, double dir)
{
  const double tol = 1e-9 * r;
  double s = 1.0;
  for (const Vector3d& n : j.faceNormals) {
    const double g0 = dir * n.dot(C0 - vj) - r;
    const double g1 = dir * n.dot(C1 - vj) - r;
    if (g1 >= -tol) continue;   // still clear of this wall at the vertex
    if (g0 <= g1) continue;     // never clear of it on this segment either
    s = std::min(s, std::max(0.0, g0 / (g0 - g1)));
  }
  return s;
}

// The section a fraction s of the way from a to b. The spine, its walls and the
// arc all vary linearly along a straight mesh edge, which is the only place a
// truncation point ever falls, so interpolating the section's points is exact
// there and degrades gracefully where the walls turn.
RoundSection lerpSection(const RoundSection& a, const RoundSection& b, double s)
{
  RoundSection out;
  if (!a.valid || !b.valid) return out;
  for (size_t k = 0; k < a.w.size(); ++k) out.w[k] = a.w[k] + s * (b.w[k] - a.w[k]);
  out.u.resize(std::min(a.u.size(), b.u.size()));
  for (size_t k = 0; k < out.u.size(); ++k) out.u[k] = a.u[k] + s * (b.u[k] - a.u[k]);
  out.v = a.v + s * (b.v - a.v);
  out.C = a.C + s * (b.C - a.C);
  // The larger of the two, not the interpolation: what stands past a wall
  // between two stations is whichever of them reaches further.
  out.eps = std::max(a.eps, b.eps);
  out.valid = true;
  return out;
}

// A solid's vertices, for handing a convex one to a hull alongside points of its
// own.
std::vector<Vector3d> hullPoints(const manifold::Manifold& solid)
{
  const manifold::MeshGL64 mesh = solid.GetMeshGL64();
  std::vector<Vector3d> pts;
  if (mesh.numProp < 3) return pts;
  pts.reserve(mesh.vertProperties.size() / mesh.numProp);
  for (size_t k = 0; k + 2 < mesh.vertProperties.size(); k += mesh.numProp)
    pts.emplace_back(mesh.vertProperties[k], mesh.vertProperties[k + 1],
                     mesh.vertProperties[k + 2]);
  return pts;
}

// The distance from the origin to the nearest face plane of a convex solid
// centred there — for a tessellated ball, the radius it actually cuts to rather
// than the one it was asked for.
double inradius(const manifold::Manifold& convex)
{
  const manifold::MeshGL64 mesh = convex.GetMeshGL64();
  if (mesh.numProp < 3) return 0.0;
  auto vert = [&mesh](size_t i) {
    const size_t k = i * mesh.numProp;
    return Vector3d(mesh.vertProperties[k], mesh.vertProperties[k + 1],
                    mesh.vertProperties[k + 2]);
  };

  double least = std::numeric_limits<double>::infinity();
  for (size_t t = 0; t + 2 < mesh.triVerts.size(); t += 3) {
    const Vector3d a = vert(mesh.triVerts[t]);
    const Vector3d nrm = (vert(mesh.triVerts[t + 1]) - a).cross(vert(mesh.triVerts[t + 2]) - a);
    const double twiceArea = nrm.norm();
    if (twiceArea < 1e-30) continue;
    least = std::min(least, std::abs(nrm.dot(a)) / twiceArea);
  }
  return std::isfinite(least) ? least : 0.0;
}

// The profile a corner cell reaches down one chain with: the same four corners
// of the bead's cross-section the wedge's pentagon is built on — the crease
// point and the two tangency points — but taken `over` past their walls instead
// of `eps`, one copy of the crease point per wall.
//
// Handing the wedge's own pentagon over instead, which is what this replaces,
// put five of the corner cell's points exactly on the wedge's surface, since a
// section of a chain is by construction where the cells hulled from it end. Two
// solids whose boundaries touch along the five edges of a shared face and then
// leave each other at a fraction of a degree are what a union cannot resolve:
// on a spike, where the cell's faces and the bead's differ by less than that, it
// left nine triangles of no area at the one height each bead was cut back at.
// Rebuilt at `over`, every point of this profile is further past its wall than
// anything the wedge has there, so the two surfaces are a clear 0.5 eps apart
// along each wall and cross transversally where the profile turns the tangency
// point — the same ladder the wedge and the arc already stand on.
//
// Past the walls is the only direction the profile may grow: outward is free
// space no ball reaches, and material there would be a lump. So its fourth side
// — the chord between the two tangency points — comes back toward the crease by
// `over * cos(phi/2)` instead. That costs nothing, because the chord is not a
// surface of the blend: it is the far side of a circular segment that lies
// wholly inside the arc, `r * (1 - sin(phi/2))` deep, which the subtraction
// removes whatever the cell does there. The two are comparable only as phi
// approaches 180 degrees, and a crease that flat is refused a section at all.
std::array<Vector3d, 4> cornerProfile(const RoundSection& s, double over)
{
  // TA = C - dir*r*nA by construction, so the two tangency points give back the
  // wall normals with the sign the section was built at, without the section
  // having to carry them.
  const Vector3d dnA = (s.C - s.w[0]).normalized() * over;
  const Vector3d dnB = (s.C - s.w[1]).normalized() * over;
  return {s.w[0] - dnA, s.w[1] - dnB, s.v - dnA, s.v - dnB};
}

// The corner cell: what the incident wedges no longer cover once they have been
// cut back, hulled from the junction vertex, the point at which each corner ball
// touches each wall it is seated against, and a profile down every chain that
// meets there. Convex by construction, so the hull is faithful; the corner balls
// are taken out of it by the same global subtraction as the canals.
//
// Every point of it is taken `over` past a wall rather than on one, for the
// reason the edge cells' pentagon is: a face resting exactly on a wall asks a
// boolean to resolve two coincident surfaces. Each goes out along its own wall's
// normal, one copy of the vertex per wall, so that every point this cell has
// near a wall is the same distance past it and the hull's face there is that
// wall's plane exactly. Displacing the vertex once along the bisector instead
// put it short of that plane by the cosine, which tilted the face by a hair and
// left a flake of the model unblended along the junction, at a thickness that
// scaled with `over`.
//
// `over` must then be larger than the edge cells' own overshoot rather than
// smaller. Equal, and the corner cell's wall face lands in their plane and the
// coincidence is back, inside the tool this time; smaller, and it grazes just
// beneath it, which is worse. Larger, and the two cross at a real angle.
manifold::Manifold cornerCell(const Junction& j, const Vector3d& vj,
                              const std::vector<std::array<Vector3d, 4>>& endProfiles, double r,
                              double dir, double over)
{
  std::vector<manifold::vec3> pts;
  auto add = [&pts](const Vector3d& p) { pts.emplace_back(p.x(), p.y(), p.z()); };

  for (const Vector3d& n : j.faceNormals) add(vj - dir * over * n);
  for (const Vector3d& P : j.ballCentres)
    for (const Vector3d& n : j.faceNormals) {
      // Only the walls this ball is actually seated against; the others it
      // merely clears, and projecting onto them would reach outside the corner.
      if (std::abs(dir * n.dot(P - vj) - r) > 1e-6 * r) continue;
      add(P - dir * (r + over) * n);
    }
  for (const auto& profile : endProfiles)
    for (const Vector3d& p : profile) add(p);

  return manifold::Manifold::Hull(pts);
}

}  // namespace

manifold::Manifold buildRoundSolid(const MergedMesh& m,
                                   const std::map<EdgeKey, std::vector<int>>& adj,
                                   const std::vector<Chain>& chains, double r, bool concave,
                                   int arcSegments, double thresholdDeg,
                                   const std::set<int>& noCorner)
{
  if (!(r > 0)) return {};

  const double dir = concave ? 1.0 : -1.0;
  const double eps = std::max(1e-3 * r, 1e-9);
  const int segs = std::max(arcSegments, 3);

  // Under the seam rule a vertex that an unfilleted crease leaves gets no corner
  // cell and no corner ball: the two beads that do arrive meet each other in a
  // seam along that crease instead. Nothing else about the vertex changes here —
  // what the rule does with it is applied further down, where the cell, the ball
  // and the grouping of the subtraction are decided.
  const std::set<EdgeKey> filleted = filletedEdges(chains);
  const std::vector<Junction> junctions = chainJunctions(m, adj, chains, r, concave, noCorner);
  // Vertices some bead reaches on a curve. The seam rule carries a bead past the
  // vertex along the line its last segment lies on, which is the crease's own
  // continuation only where the crease arrives straight; see arrivesStraight.
  std::set<int> arrivesBent;
  for (const Chain& c : chains) {
    if (c.closed || c.verts.size() < 2) continue;
    for (const bool front : {true, false})
      if (!arrivesStraight(m, c, front))
        arrivesBent.insert(front ? c.verts.front() : c.verts.back());
  }
  // Every use below asks the same question of the same vertex, and answering it
  // walks the edge table, so it is answered once.
  std::map<int, bool> seamCache;
  auto seamVertex = [&](int v) {
    const auto it = seamCache.find(v);
    if (it != seamCache.end()) return it->second;
    const bool yes = arrivesBent.count(v) == 0 &&
                     creaseLeavesUnfilleted(m, adj, filleted, thresholdDeg, v);
    seamCache.emplace(v, yes);
    return yes;
  };
  // How far a bead arriving at a seam vertex runs past it, as a fraction of the
  // radius. A tenth is the measured floor: it is far enough that the two beads
  // overlap over a region at every opening angle and every tessellation measured,
  // and short enough to stay well inside the wall it runs into where there is a
  // wall to stay inside of — where there is less room than that, seamRoom below
  // takes what there is instead. Longer than about twice this the overrun is
  // longer than the corner it is repairing and starts writing over the beads
  // either side of it, so there is no room above to move into either.
  constexpr double seamOver = 0.10;

  std::map<int, const Junction *> junctionAt;
  for (const Junction& j : junctions) {
    // A seam vertex builds no corner cell, so nothing is going to fill what
    // truncating the spines into it would empty. Leaving it out of the map is
    // what stops the truncation, and hands the vertex to the rule below that has
    // the two beads meet each other instead. The brush already withheld the
    // junction at the corners it shortened; this is the same withholding at the
    // corners a crease is missing from for any other reason.
    //
    // Not gated on the overrun: the vertex gets no ball whatever the overrun is,
    // and truncating into a corner that nothing then fills is a hole either way.
    if (seamVertex(j.vert)) continue;
    junctionAt[j.vert] = &j;
  }

  std::vector<std::vector<RoundSection>> sections;
  sections.reserve(chains.size());
  for (const Chain& chain : chains)
    sections.push_back(roundSections(m, adj, chain, r, concave, segs, thresholdDeg));

  // Where each section sits along its chain, as the chain parameter the brushes'
  // selection is written in. It starts as the identity — one section per station
  // — and stops being it below: truncation slides an end section back off its
  // station, and a runout replaces one with a fan of them. Carrying it is what
  // lets the selection stay in the space the caller's brush cut it in, which is
  // the only space where a brush boundary is a fixed physical point.
  std::vector<std::vector<double>> sectionAt(chains.size());
  // Where each chain's own far end sits in that space before anything moves it,
  // which is the parameter a selection reaching the end of the chain reaches.
  std::vector<double> chainEndParam(chains.size(), 0.0);
  for (size_t ci = 0; ci < chains.size(); ++ci) {
    sectionAt[ci].resize(sections[ci].size());
    for (size_t i = 0; i < sectionAt[ci].size(); ++i) sectionAt[ci][i] = static_cast<double>(i);
    chainEndParam[ci] = sectionAt[ci].empty() ? 0.0 : sectionAt[ci].back();
  }

  // Cut every chain back where its ball first meets a wall of the junction it
  // runs into, and hand that junction the truncated end for its corner cell to
  // reach with. Both ends come off the untruncated sections, so a two-station
  // chain does not truncate itself twice over. What is handed over is the section
  // rather than the profile built from it, because how far past the walls that
  // profile has to stand is not known until every chain arriving there has said
  // how far it stands itself.
  std::map<int, std::vector<RoundSection>> endSections;
  std::vector<bool> chainUsable(chains.size(), true);
  std::vector<std::array<bool, 2>> runout(chains.size(), {false, false});
  for (size_t ci = 0; ci < chains.size(); ++ci) {
    const Chain& chain = chains[ci];
    std::vector<RoundSection>& sec = sections[ci];
    const size_t n = sec.size();
    if (chain.closed || n < 2) continue;

    std::vector<RoundSection> trimmed = sec;
    double consumed = 0.0;  // of the single segment a two-station chain has
    for (const bool front : {true, false}) {
      const size_t endIdx = front ? 0 : n - 1;
      const size_t nbrIdx = front ? 1 : n - 2;
      const auto it = junctionAt.find(chain.verts[endIdx]);
      if (it == junctionAt.end()) continue;
      const RoundSection& a = sec[nbrIdx];
      const RoundSection& b = sec[endIdx];
      if (!a.valid || !b.valid) continue;

      // A junction whose solve found no ball centre gets no corner cell, so
      // truncating into it would cut every incident spine back and leave the
      // space they vacated empty. Ramp the radius down to nothing over the last
      // stretch instead: the beads converge on the sharp vertex, the corner
      // closes with no patch, and the blend fades out locally rather than
      // stopping with a step.
      if (it->second->ballCentres.empty()) {
        runout[ci][front ? 0 : 1] = true;
        continue;
      }

      const Junction& j = *it->second;
      double s = truncationParam(j, m.pos[j.vert], a.C, b.C, r, dir);

      // Stopping exactly at the corner puts the canal's last section flat on the
      // corner ball — the section is a great disc of it — and two subtracted
      // surfaces that coincide over a whole face rather than crossing are what a
      // Nef kernel downstream chokes on. Run a hair past, so they cross. The
      // overshoot is the same trick and the same size as the pentagon's at the
      // walls, and what it takes extra is a ring a thousandth of a radius deep
      // inside the corner cell, which the corner ball removes anyway.
      const double step = (b.C - a.C).norm();
      const double nudge = step > 1e-12 ? 1e-3 * r / step : 0.0;

      // Zero means the ball is already blocked at the neighbour station, so the
      // whole of the last segment is inside the corner and there is no point
      // along it for the bead to stop at. Where a crease runs into a junction its
      // stations crowd — on a pipe through a boss they are a fifth of the radius
      // apart there against a whole radius further along — so this is ordinary
      // rather than exceptional. Nudged off zero it leaves a cell the width of
      // the nudge, a sliver a thousandth of a radius long whose end faces and
      // seam cover are both degenerate. Drop it and let the bead end at the
      // neighbour station, which is the first station outside the corner anyway.
      //
      // Three stations with both ends dropped is the one case here that is
      // reasoned about rather than measured. It would leave only the middle
      // section valid, so every cell has an invalid end and the bead vanishes
      // without saying so, with the two corner cells nearly meeting on that
      // station. Reaching it needs a crease shorter than about two radii running
      // between two junctions, which is what the size gate refuses as crowded, so
      // it is unreachable through the node and only a direct caller could build
      // it. Nothing here is written for it: over the whole test suite and a
      // 540-configuration sweep the shortest chain this branch was ever taken on
      // had seventeen stations.
      if (s <= 0.0) {
        // Two stations and nothing left between them is no bead at all.
        if (n == 2) {
          chainUsable[ci] = false;
          continue;
        }
        trimmed[endIdx].valid = false;
        // Nothing is built out here now, so the entry says the station the bead
        // ends at rather than the one this section used to sit on.
        sectionAt[ci][endIdx] = static_cast<double>(nbrIdx);
        // The corner cell still has to overlap the bead in a slab rather than
        // meet it on the neighbour station's plane, so it reaches a hair past
        // that station into the segment beyond. That is a different segment from
        // the one `nudge` was measured on, and they are not the same length: a
        // fraction of a segment is only a distance once the segment it is a
        // fraction of is named. Capped, since a segment shorter than the hair
        // itself would otherwise ask for a fraction past the far end of it.
        const RoundSection& in = sec[front ? 2 : n - 3];
        const double back = (a.C - in.C).norm();
        const double hair = back > 1e-12 ? std::min(1e-3 * r / back, 0.05) : 0.05;
        const RoundSection reach = lerpSection(in, a, 1.0 - 2.0 * hair);
        if (reach.valid) endSections[j.vert].push_back(reach);
        continue;
      }

      if (s < 1.0) s = std::min(1.0, s + nudge);
      consumed += 1.0 - s;
      trimmed[endIdx] = lerpSection(a, b, s);
      // s runs from the neighbour station toward the end one, so the section now
      // sits a fraction s of the way from `nbrIdx` to `endIdx` in chain terms.
      sectionAt[ci][endIdx] = static_cast<double>(nbrIdx) +
                              s * (static_cast<double>(endIdx) - static_cast<double>(nbrIdx));

      // The corner cell reaches a hair further back than the section the wedge
      // stops at, so the two overlap in a slab instead of meeting on one shared
      // face. Cells that merely abut leave the union a face that is in both of
      // them and in neither's interior, and the sub-micron triangles that come of
      // it survive as far as the first kernel that quantises its input. What it
      // reaches with is not that section's own pentagon but a profile rebuilt at
      // the corner cell's distance past the walls; cornerProfile says why.
      const RoundSection reach = lerpSection(a, b, std::max(0.0, s - 2.0 * nudge));
      if (reach.valid) endSections[j.vert].push_back(reach);
    }

    // A chain one segment long, truncated from both ends by more than its own
    // length, has nothing left between the two corner cells — and interpolating
    // past the crossing point would build a cell inside out. Drop it; the corner
    // cells meet each other there.
    if (n == 2 && consumed > 1.0) chainUsable[ci] = false;
    sec = std::move(trimmed);
  }

  // Replace the end station of every chain that runs out with a ramp: a few
  // samples along the last stretch, each carrying its own radius, ending in the
  // sharp vertex itself. Sections are no longer one per station past this point,
  // which is fine — a cell is the hull of two consecutive sections whatever
  // produced them.
  for (size_t ci = 0; ci < chains.size(); ++ci) {
    if (!runout[ci][0] && !runout[ci][1]) continue;
    const Chain& chain = chains[ci];
    const std::vector<StationNormals> stations = chainNormals(m, adj, chain);
    const std::vector<RoundSection>& sec = sections[ci];
    if (stations.size() != sec.size() || sec.size() < 2) continue;

    // Over how much of the crease the bead fades. Two radii is short enough to
    // stay local and long enough that the taper is gentler than the bead's own
    // curvature; a segment shorter than that ramps over what it has.
    const double rampLength = 2.0 * r;

    // Each sample carries the chain parameter it was taken at, so the selection
    // can still be read against a chain whose sections no longer line up with
    // its stations.
    auto ramp = [&](size_t endIdx, size_t nbrIdx, std::vector<RoundSection>& into,
                    std::vector<double>& atInto) {
      const StationNormals& end = stations[endIdx];
      const StationNormals& nbr = stations[nbrIdx];
      if (!end.valid || !nbr.valid) return;
      const double segment = (end.v - nbr.v).norm();
      if (segment < 1e-12) return;
      const double length = std::min(rampLength, segment);
      const double toward = static_cast<double>(nbrIdx) - static_cast<double>(endIdx);

      // Samples run from the vertex outward, closest first; the caller puts them
      // in the order its end needs.
      constexpr int kSamples = 6;
      RoundSection tip;
      tip.w.fill(end.v);
      tip.u.assign(1, end.v);
      tip.v = end.v;
      tip.C = end.v;
      tip.valid = true;
      into.push_back(std::move(tip));
      atInto.push_back(static_cast<double>(endIdx));

      for (int k = 1; k <= kSamples; ++k) {
        const double d = length * k / kSamples;
        // The last sample lands on the neighbour station when the ramp is as
        // long as the segment; that station is already in the list.
        if (segment - d < 1e-9 * segment) break;
        const double s = d / segment;  // from the end vertex toward the neighbour
        const Vector3d v = end.v + s * (nbr.v - end.v);
        Vector3d nA = (1.0 - s) * end.nA + s * nbr.nA;
        Vector3d nB = (1.0 - s) * end.nB + s * nbr.nB;
        if (nA.norm() < 1e-9 || nB.norm() < 1e-9) return;
        const double radius = r * d / length;
        SpineFrame f;
        f.v = v;
        f.nA = nA.normalized();
        f.nB = nB.normalized();
        // The walls are the end station's, walked from its triangles; the sample
        // is at most a couple of radii away along the same two of them.
        f.triA = end.triA;
        f.triB = end.triB;
        into.push_back(
          makeRoundSection(m, adj, f, radius, concave, segs, std::max(1e-3 * radius, 1e-9),
                           thresholdDeg));
        atInto.push_back(static_cast<double>(endIdx) + s * toward);
      }
    };

    // The ramp is built vertex-first, which is already the order the front end
    // wants; the back end takes the same list reversed.
    std::vector<RoundSection> rebuilt;
    std::vector<double> rebuiltAt;
    if (runout[ci][0]) ramp(0, 1, rebuilt, rebuiltAt);
    for (size_t i = runout[ci][0] ? 1 : 0; i + (runout[ci][1] ? 1 : 0) < sec.size(); ++i) {
      rebuilt.push_back(sec[i]);
      rebuiltAt.push_back(sectionAt[ci][i]);
    }
    if (runout[ci][1]) {
      std::vector<RoundSection> back;
      std::vector<double> backAt;
      ramp(sec.size() - 1, sec.size() - 2, back, backAt);
      rebuilt.insert(rebuilt.end(), back.rbegin(), back.rend());
      rebuiltAt.insert(rebuiltAt.end(), backAt.rbegin(), backAt.rend());
    }

    sections[ci] = std::move(rebuilt);
    sectionAt[ci] = std::move(rebuiltAt);
  }

  // Chain ends that land on a vertex another chain also ends at, and that got no
  // corner cell to close them, stop a hair short of it.
  //
  // Something is always refused there or the corner would have been built - a
  // third crease too small for the size, most often - and the beads that were
  // built still arrive at the same point. Stopping both on it has them touch
  // along one line, with the material of each falling away either side, and a
  // touch is what the caller's union resolves into a flap of zero thickness. The
  // hair of crease left bare between them is bare either way: it is the corner
  // the refused crease runs into, which nothing was going to blend.
  //
  // A seam vertex goes the other way, and runs each bead PAST the vertex instead.
  // The hair does not actually separate the two surfaces where it matters: the
  // top of a bead's cross-section is tangent to its own wall, that wall is the one
  // the other bead's crease ends on, and the two tops therefore converge on the
  // same point of the sharp edge however far back either bead stops. They meet
  // there at no angle at all, and a boolean can only resolve that into a knife
  // edge. Overlapping them over a region instead makes the meeting an ordinary
  // transversal crossing, and the seam an ordinary intersection curve.
  {
    std::map<int, int> endsAt;
    for (size_t ci = 0; ci < chains.size(); ++ci) {
      if (!chainUsable[ci] || chains[ci].closed || chains[ci].verts.size() < 2) continue;
      ++endsAt[chains[ci].verts.front()];
      ++endsAt[chains[ci].verts.back()];
    }

    for (size_t ci = 0; ci < chains.size(); ++ci) {
      if (!chainUsable[ci] || chains[ci].closed) continue;
      for (const bool front : {true, false}) {
        std::vector<RoundSection>& sec = sections[ci];
        if (sec.size() < 2) break;
        const int vert = front ? chains[ci].verts.front() : chains[ci].verts.back();
        if (endsAt[vert] < 2) continue;             // nothing else ends here
        if (junctionAt.count(vert) != 0) continue;  // a corner already answers for it

        const size_t endIdx = front ? 0 : sec.size() - 1;
        const size_t nbrIdx = front ? 1 : sec.size() - 2;
        const double step = (sec[endIdx].v - sec[nbrIdx].v).norm();
        if (step < 1e-12) continue;
        // The hair, or a twentieth of the last segment where that is shorter: a
        // crease whose stations are the intersection curve of two curved walls
        // has segments a fraction of the hair long, and taking the hair off one
        // of those is taking off the whole cell.
        double back = std::min(eps / step, 0.05);
        // At a seam vertex, run the bead PAST the vertex instead of stopping
        // short of it.
        //
        // Stopping short leaves the two beads a hair apart with their surfaces
        // parallel across the gap; running exactly to the vertex has them touch.
        // Either way the two surfaces meet at no angle at the one place they both
        // reach - the top of each bead's cross section, where it is tangent to the
        // wall the other bead's own wall crosses - and a boolean can only resolve
        // that into a knife edge. Overrunning makes the two bodies overlap over a
        // region instead, so their surfaces cross transversally and the seam is an
        // ordinary intersection curve.
        //
        // Past the vertex the bead is inside the wall the refused crease runs
        // along - solid for a concave blend the caller unions on, outside the
        // part for a convex one the caller cuts with - so where that wall goes on
        // past the vertex the overrun costs nothing. Where it does not, seamRoom
        // says how much of it there is and the overrun takes a share of that
        // instead; a bead that ran past the far side of a thin wall would stand
        // proud of a face nothing was blending.
        if (seamVertex(vert)) {
          std::vector<Vector3d> swept(sec[endIdx].w.begin(), sec[endIdx].w.end());
          swept.push_back(sec[endIdx].v);
          const Vector3d dir = (sec[endIdx].v - sec[nbrIdx].v) / step;
          const double want = seamOver * r;
          // Half the room, never all of it: landing the bead's end exactly on the
          // far face puts two coincident surfaces in front of the caller's
          // boolean, which is the thing the overrun exists to avoid. Looking out
          // to twice what is wanted is therefore looking exactly as far as it can
          // matter — past that the half is more than the whole of the want.
          const double room = seamRoom(m, vert, swept, dir, 2.0 * want);
          const double over = std::min(want, 0.5 * room);
          if (over > 1e-12) {
            back = -std::min(over / step, 0.5);
          } else {
            // No room at all: the bead is against a wall thinner than its own
            // profile and there is nothing past the vertex to run into. The two
            // beads cannot be made to overlap, so make sure they do not merely
            // graze either — stopping them a definite distance short instead of a
            // hair short turns a tangential touch into a plain gap, which a
            // boolean can resolve. Half the overrun, by the same halving rule the
            // room is shared out under. Only ever less material, never more, so
            // it cannot put the bead through the wall the overrun was refused for.
            back = std::max(back, std::min(0.5 * want / step, 0.5));
          }
        }
        const RoundSection stop = lerpSection(sec[nbrIdx], sec[endIdx], 1.0 - back);
        if (!stop.valid) continue;

        sec[endIdx] = stop;
        // The section moved, so where it sits in chain-parameter space moves with
        // it, overrun and hair alike. Leaving the parameter behind would not hold
        // the caller's brush boundaries still - it would reparameterise the whole
        // last segment under them, and slide every boundary on it by a share of
        // the overrun. The overrun itself is still reached: a run of the selection
        // that asked for the chain's own end asks for a parameter beyond the last
        // station now, and toSectionSpace clamps it onto that station.
        sectionAt[ci][endIdx] -= (sectionAt[ci][endIdx] - sectionAt[ci][nbrIdx]) * back;
      }
    }
  }

  // Read the selection against where the sections ended up. Everything from here
  // works in section-index space — the cells are hulls of consecutive sections —
  // while the intervals were cut in chain-parameter space, and truncation and
  // runout have moved the two apart at the ends of every chain that meets a
  // junction.
  //
  // An empty list means the whole chain downstream, so a selection that maps to
  // nothing cannot be passed on as one: the two say opposite things. A run lying
  // wholly inside the stretch a junction truncates away has no sections left in
  // it, and the material it asked for is the corner cell's already — so the
  // chain builds no bead, which is what the brush asked for. Whether the corner
  // itself is built is a separate question, already settled by endAnchored.
  //
  // A run that reaches an end of the chain reaches whatever that end became. An
  // overrun puts the end station past the chain's own last parameter, and a run
  // stopping at that last parameter would then stop short of it — the caller
  // asked for the whole crease and would get it minus the overrun at each end.
  // So a run touching an end is carried out to where the end went. Only ever
  // outward: an end a junction truncated sits inside the chain instead, and the
  // clamp in toSectionSpace already answers for that.
  std::vector<std::vector<SpineInterval>> keepOf(chains.size());
  for (size_t ci = 0; ci < chains.size(); ++ci) {
    std::vector<SpineInterval> keep = chains[ci].keep;
    if (!chains[ci].closed && sectionAt[ci].size() >= 2) {
      for (SpineInterval& iv : keep) {
        if (iv.first <= 1e-12) iv.first = std::min(iv.first, sectionAt[ci].front());
        if (iv.second >= chainEndParam[ci] - 1e-12)
          iv.second = std::max(iv.second, sectionAt[ci].back());
      }
    }
    keepOf[ci] = toSectionSpace(keep, sectionAt[ci], chains[ci].closed);
    if (keepOf[ci].empty() && !chains[ci].keep.empty()) chainUsable[ci] = false;
  }

  // Group the subtraction by the chains a corner ball actually ties together,
  // instead of globally.
  //
  // The recorded reason the subtraction is global is that a corner ball has to
  // cut the wedges of every chain meeting at its vertex. That requirement is
  // local to the vertex, so it is met by putting exactly those chains in one
  // group. Where no ball is built the requirement is vacuous, and the chains may
  // be kept apart — which is what lets two beads arriving at a brush-shortened
  // corner run into each other and leave a seam instead of one canal gouging the
  // other's bead.
  //
  // chainJunctions already drops every vertex in `noCorner` — the vertices where
  // the brush covered fewer arms than the corner has creases — so the junction
  // list is precisely the set of corners that get a ball, and linking the chains
  // at those vertices is the whole rule. At a corner where every crease is
  // selected, all its chains land in one group and the result is the global
  // subtraction unchanged.
  //
  // Chains are linked only where a ball is built, so a vertex that emits no
  // junction at all links nothing — and a brushed corner emits none, because the
  // brush pass already put it in `noCorner`. Splitting the subtraction there is
  // only ever a means to serve a seam vertex, so where the rule serves none the
  // grouping must be the one it would have had without it: the split is what
  // lets two beads meet in a seam, and with no seam to make it is a plain loss of
  // the coincident-face cover a single subtraction gives. So the grouping is
  // taken from what the rule actually served — no vertex served, one group.
  std::vector<int> parent(chains.size());
  for (size_t i = 0; i < chains.size(); ++i) parent[i] = static_cast<int>(i);
  std::function<int(int)> findRoot = [&](int x) {
    while (parent[x] != x) x = parent[x] = parent[parent[x]];
    return x;
  };
  std::map<int, std::vector<int>> chainsAt;  // vertex -> chains with an end there
  for (size_t ci = 0; ci < chains.size(); ++ci) {
    if (chains[ci].closed || chains[ci].verts.size() < 2) continue;
    chainsAt[chains[ci].verts.front()].push_back(static_cast<int>(ci));
    chainsAt[chains[ci].verts.back()].push_back(static_cast<int>(ci));
  }
  // Whether a chain is kept apart is asked of the chain, not of the model. A
  // model can carry a served corner in one place and a withheld one in another,
  // and one flag for the whole call gives the served corner's answer to every
  // withheld one: their chains meet at a vertex that emits no junction, so
  // nothing links them and each subtracts alone — the split with none of the
  // seam it exists to make.
  //
  // So a chain no served seam vertex touches is put back in the one group
  // everything was in before the rule existed. A seam is two beads meeting, so a
  // lone chain end has no seam to make and does not count as served.
  auto servedEnd = [&](size_t ci) {
    if (chains[ci].closed || chains[ci].verts.size() < 2) return false;
    for (const int v : {chains[ci].verts.front(), chains[ci].verts.back()}) {
      const auto it = chainsAt.find(v);
      if (it != chainsAt.end() && it->second.size() > 1 && seamVertex(v)) return true;
    }
    return false;
  };
  int rest = -1;
  for (size_t i = 0; i < chains.size(); ++i) {
    if (servedEnd(i)) continue;
    if (rest < 0) rest = static_cast<int>(i);
    else parent[findRoot(static_cast<int>(i))] = findRoot(rest);
  }
  // A ball has to cut the wedges of every chain meeting at its vertex, so those
  // chains are one group wherever one is built. A seam vertex builds none, and
  // there the chains are left as they are.
  for (const Junction& j : junctions) {
    if (seamVertex(j.vert)) continue;
    const auto it = chainsAt.find(j.vert);
    if (it == chainsAt.end()) continue;
    for (size_t k = 1; k < it->second.size(); ++k) {
      const int a = findRoot(it->second[0]), b = findRoot(it->second[k]);
      if (a != b) parent[a] = b;
    }
  }

  std::vector<manifold::Manifold> wedgeCells, canalCells;
  std::vector<int> wedgeGroup, canalGroup;
  // Tag every cell appended since the vector was last sized with its group, so
  // the two stay parallel however many cells a helper chose to add.
  auto tag = [](const std::vector<manifold::Manifold>& cells, std::vector<int>& groups, int g) {
    groups.resize(cells.size(), g);
  };
  for (size_t ci = 0; ci < chains.size(); ++ci) {
    if (!chainUsable[ci]) continue;
    const int gid = findRoot(static_cast<int>(ci));
    const size_t n = sections[ci].size();
    const size_t segments = n < 2 ? 0 : (chains[ci].closed ? n : n - 1);
    const std::vector<std::vector<SpineBulge>> bulges =
      chainBulges(m, adj, chains[ci], sectionAt[ci], concave);
    appendChainCells(chains[ci], sections[ci],
                     [](const RoundSection& s) -> const std::array<Vector3d, 5>& { return s.w; },
                     lerpSection, keepOf[ci], bulges, wedgeCells);
    appendChainCells(chains[ci], sections[ci],
                     [](const RoundSection& s) -> const std::vector<Vector3d>& { return s.u; },
                     lerpSection, overhang(keepOf[ci], segments), {}, canalCells);

    // Both unions are cut at the same stations, so every one of them hands the
    // subtraction a pair of coincident planes. Cover the seams of each.
    appendSeamCovers(chains[ci], sections[ci],
                     [](const RoundSection& s) -> const std::array<Vector3d, 5>& { return s.w; },
                     [](const RoundSection& s) { return sectionAnchor(s.w); }, keepOf[ci],
                     wedgeCells);
    appendSeamCovers(chains[ci], sections[ci],
                     [](const RoundSection& s) -> const std::vector<Vector3d>& { return s.u; },
                     [](const RoundSection& s) { return s.C; },
                     overhang(keepOf[ci], segments), canalCells);
    tag(wedgeCells, wedgeGroup, gid);
    tag(canalCells, canalGroup, gid);
  }

  // The corner ball as a point set, so the hull below can take it together with
  // the points that carry it past a wall. Hulling the vertices of a convex
  // polyhedron gives that polyhedron back, so the ball is unchanged by being
  // passed this way.
  const manifold::Manifold ball = manifold::Manifold::Sphere(r, segs);
  const std::vector<Vector3d> ballShell = hullPoints(ball);

  // How far past its walls one corner stands: further than any bead arriving
  // there, since a corner cell resting inside one of them is the coincident-face
  // problem the overshoot exists to avoid, and the beads no longer all stand the
  // same distance past a wall.
  auto epsAt = [&](const Junction& j) {
    double most = eps;
    for (const RoundSection& s : endSections[j.vert]) most = std::max(most, s.eps);
    return most;
  };

  // How far short of a wall a tessellated ball seated against it stops. Its
  // vertices are on the sphere and its faces are therefore chords, so the face
  // that meets the wall reaches only that face's own distance from the centre,
  // and the ball is short of the wall by the rest everywhere on it but its
  // corners. At the tessellations these tools are drawn at, that shortfall is
  // several times the whole overshoot ladder, so it is what a point meant to
  // stand past the wall has to clear first. Measured off the mesh rather than
  // assumed from the segment count, because it is the mesh that does the cutting.
  const double ballShort = r - inradius(ball);

  for (const Junction& j : junctions) {
    if (j.ballCentres.empty()) continue;
    // A sharp edge leaves this vertex: the beads meet each other in a seam along
    // it, and a ball would round that edge's own start away.
    if (seamVertex(j.vert)) continue;
    const double here = epsAt(j);
    // Where the corner cell stands relative to the beads' overshoot and the
    // arc's two: see cornerCell for why it is between them rather than at either.
    const double over = 1.5 * here;
    const double ballPast = ballShort + 2.0 * here;
    std::vector<std::array<Vector3d, 4>> profiles;
    for (const RoundSection& s : endSections[j.vert]) profiles.push_back(cornerProfile(s, over));
    manifold::Manifold cell = cornerCell(j, m.pos[j.vert], profiles, r, dir, over);
    if (cell.IsEmpty()) continue;
    // The group the ball's own vertex ties together: every chain arriving there
    // was unioned into one component above, so the ball lands in the same group
    // as all the wedges it has to cut.
    const auto cit = chainsAt.find(j.vert);
    const int gid = cit == chainsAt.end() ? 0 : findRoot(cit->second[0]);
    wedgeCells.push_back(std::move(cell));
    tag(wedgeCells, wedgeGroup, gid);
    // One ball per reachable centre, hulled together. The hull is not an
    // approximation: dilating a convex hull of points by a ball gives the hull
    // of the balls at those points, and the centres are the corners of a convex
    // region every point of which the ball may sit at — so the hull is exactly
    // the material it can sweep out there.
    //
    // Each ball also gets one point per wall it is seated against, out past the
    // tangency. Seated means tangent, and a cutter that arrives at a wall along
    // it rather than across it leaves everything the cells stand past that wall
    // — at a corner, a lens of the overshoot around the tangency point, which the
    // canals running in sever from the rest of the tool as they cut deeper still.
    // That is what came away from a curved junction as a detached wafer, once the
    // tessellation was fine enough that the ball no longer blundered past the
    // wall by its own coarseness. The two points every arc already carries past
    // its walls are the same answer along a crease; this is it at the one place a
    // ball rather than a canal is what cuts.
    //
    // The point only deepens the cut where the cone it raises is still below the
    // wall, and above the wall the surface is the ball's own, so nothing of the
    // blend goes with it and the corner does not step away from the canals it
    // hands over to.
    std::vector<manifold::vec3> pts;
    pts.reserve(j.ballCentres.size() * (ballShell.size() + j.faceNormals.size()));
    for (const Vector3d& P : j.ballCentres) {
      for (const Vector3d& q : ballShell) {
        const Vector3d p = P + q;
        pts.emplace_back(p.x(), p.y(), p.z());
      }
      for (const Vector3d& n : j.faceNormals) {
        if (std::abs(dir * n.dot(P - m.pos[j.vert]) - r) > 1e-6 * r) continue;
        const Vector3d horn = P - dir * (r + ballPast) * n;
        pts.emplace_back(horn.x(), horn.y(), horn.z());
      }
    }
    canalCells.push_back(manifold::Manifold::Hull(pts));
    tag(canalCells, canalGroup, gid);
  }

  // One subtraction over everything. The spines stop where the ball does, so the
  // canal is exactly the set of positions the ball can occupy and nothing in it
  // is material another crease still needs — while the corner ball, conversely,
  // has to cut the wedges of every chain meeting at its vertex, which grouping
  // the subtraction per chain would prevent.
  // One subtraction per group. With the global grouping above every chain is in
  // group zero, so this is the single global subtraction unchanged.
  std::map<int, std::vector<manifold::Manifold>> wedgeOf, canalOf;
  for (size_t i = 0; i < wedgeCells.size(); ++i)
    wedgeOf[wedgeGroup[i]].push_back(std::move(wedgeCells[i]));
  for (size_t i = 0; i < canalCells.size(); ++i)
    canalOf[canalGroup[i]].push_back(std::move(canalCells[i]));

  // Volumeless parts are what a subtraction leaves behind, so a wedge that never
  // met a canal has none to drop and is handed back exactly as it was built —
  // which is what HEAD does with the one group the grouping-off path leaves, and
  // the reason the drop is asked for once at the end rather than per group.
  std::vector<manifold::Manifold> parts;
  bool subtracted = false;
  for (auto& [gid, cells] : wedgeOf) {
    const manifold::Manifold wedge = unionCells(cells);
    if (wedge.IsEmpty()) continue;
    const auto cit = canalOf.find(gid);
    if (cit == canalOf.end()) { parts.push_back(wedge); continue; }
    const manifold::Manifold canal = unionCells(cit->second);
    if (canal.IsEmpty()) { parts.push_back(wedge); continue; }
    subtracted = true;
    parts.push_back(wedge - canal);
  }
  if (parts.empty()) return {};
  if (parts.size() == 1)
    return subtracted ? dropVolumelessParts(std::move(parts.front())) : std::move(parts.front());
  return dropVolumelessParts(unionCells(parts));
}

std::unique_ptr<PolySet> debugEdgeMarkers(
  const MergedMesh& m, const std::map<EdgeKey, std::vector<int>>& adj, double thresholdDeg)
{
  if (m.pos.empty()) return nullptr;
  const double half = markerHalf(m);

  // Below this dihedral a "crease" is really a triangulation diagonal inside a
  // flat face, not an edge of the shape — skip it so flat faces stay clean.
  constexpr double kCoplanarDeg = 1.0;

  const Color4f concaveColor(0.90f, 0.20f, 0.20f, 1.0f);
  const Color4f convexColor(0.25f, 0.80f, 0.30f, 1.0f);
  const Color4f seamColor(0.55f, 0.55f, 0.62f, 1.0f);

  PolySetBuilder builder(0, 0, 3, /*convex=*/false);
  bool any = false;
  for (const auto& [key, ts] : adj) {
    if (ts.size() != 2) continue;
    const EdgeClass ec = classifyEdge(m, key, m.tris[ts[0]], m.tris[ts[1]]);
    if (ec.dihedralDeg < kCoplanarDeg) continue;  // flat-face diagonal
    const Color4f color = !isFeatureAngle(ec.dihedralDeg, thresholdDeg) ? seamColor
                          : ec.concave                                  ? concaveColor
                                                                        : convexColor;
    addBoxMarker(builder, m.pos[key.first], m.pos[key.second], half, color);
    any = true;
  }

  if (!any) return nullptr;
  return builder.build();
}

std::unique_ptr<PolySet> debugSpineMarkers(const MergedMesh& m,
                                           const std::vector<SpineFrame>& frames)
{
  if (m.pos.empty()) return nullptr;
  const double half = markerHalf(m);

  const Color4f centerColor(0.20f, 0.45f, 0.95f, 1.0f);  // ball center C
  const Color4f taColor(0.95f, 0.60f, 0.15f, 1.0f);      // tangency on wall A
  const Color4f tbColor(0.75f, 0.30f, 0.90f, 1.0f);      // tangency on wall B
  const Color4f legColor(0.85f, 0.85f, 0.30f, 1.0f);     // vertex -> tangency legs

  PolySetBuilder builder(0, 0, 3, /*convex=*/false);
  bool any = false;
  for (const SpineFrame& f : frames) {
    if (!f.valid) continue;
    addBoxMarker(builder, f.v, f.TA, half * 0.5, legColor);
    addBoxMarker(builder, f.v, f.TB, half * 0.5, legColor);
    addCubeMarker(builder, f.TA, half * 1.1, taColor);
    addCubeMarker(builder, f.TB, half * 1.1, tbColor);
    addCubeMarker(builder, f.C, half * 1.4, centerColor);
    any = true;
  }

  if (!any) return nullptr;
  return builder.build();
}

}  // namespace fillet::detail

// Rebuild edge -> two-face adjacency from the target's triangle soup, classify
// each edge as concave/convex and feature/seam, walk the ones this tool acts on
// into chains, and build the tool solid along them. A diagnostic count line goes
// out on every invocation (a plain cube yields 12 feature edges, all convex; an
// inside corner yields a single concave edge).
std::shared_ptr<const Geometry> buildFilletTool(
  const FilletNode& node, FilletType type, const std::shared_ptr<const ManifoldGeometry>& target,
  const std::shared_ptr<const ManifoldGeometry>& brush)
{
  using namespace fillet::detail;

  if (!target || target->isEmpty()) {
    LOG(message_group::Warning, node.modinst->location(), "",
        "%1$s: target has no geometry to fillet", node.name());
    return nullptr;
  }

  const MergedMesh m = mergeMesh(target->getManifold().GetMeshGL64());
  const std::map<EdgeKey, std::vector<int>> adj = buildEdgeAdjacency(m.tris);

  // Derive the crease threshold from the tessellation parameters so that arc
  // seams the discretizer itself produces (and any the primitive produced) fall
  // below it and are not mistaken for feature edges. min_angle= overrides.
  const double thresholdDeg =
    node.min_angle >= 0 ? node.min_angle : 1.5 * node.discretizer.getMaxSeamAngle();
  // Face provenance (whether two faces trace back to the same source surface)
  // is only meaningful when the mesh carries more than one source id; a single-
  // id mesh (imported STL, polyhedron) can't rely on it.
  const bool useProvenance = m.distinctIDs.size() > 1;
  const bool wantConcave = type == FilletType::FILLET || type == FilletType::CHAMFER;

  const ClassCounts c = classifyEdges(m, adj, thresholdDeg, useProvenance);

  const size_t selected = wantConcave ? c.featureConcave : c.featureConvex;
  LOG(message_group::Echo, node.modinst->location(), "",
      "%1$s: mesh %2$d verts (%3$d merged), %4$d tris, %5$d surfaces; "
      "%6$d edges (%7$d two-face, %8$d non-manifold); feature edges %9$d "
      "(concave %10$d, convex %11$d, %12$d same-surface); selects %13$d %14$s "
      "edge(s) at %15$.1f deg",
      node.name(), static_cast<int>(m.numRawVert), static_cast<int>(m.pos.size()),
      static_cast<int>(m.tris.size()), static_cast<int>(m.distinctIDs.size()),
      static_cast<int>(adj.size()), static_cast<int>(c.twoFace),
      static_cast<int>(c.nonManifold), static_cast<int>(c.feature),
      static_cast<int>(c.featureConcave), static_cast<int>(c.featureConvex),
      static_cast<int>(c.featureSameSurface), static_cast<int>(selected),
      wantConcave ? "concave" : "convex", thresholdDeg);

  const std::vector<EdgeKey> selectedKeys = selectedEdges(m, adj, thresholdDeg, wantConcave);
  std::vector<Chain> chains = buildChains(m, selectedKeys);
  // Before the brushes are asked anything, so that there is only ever one
  // chain-parameter space and it is the one the selection is cut in.
  resampleChains(m, chains, kSliverFraction);

  // debug = true swaps the tool solid for a visualization: colored markers along
  // every edge (concave/convex/rejected), plus the spine's per-vertex tangency
  // frame (ball center and the two tangency points) walked from the selected
  // edges. It is off by default because those markers are hundreds of disjoint
  // cubes rather than one solid, and anything downstream that expects a
  // well-formed mesh — Minkowski, which falls back to CGAL's Nef kernel, above
  // all — either grinds for minutes or dies on them.
  if (node.debug) {
    std::vector<SpineFrame> frames;
    for (const Chain& chain : chains) {
      const std::vector<SpineFrame> chainFrames =
        spineFrames(m, adj, chain, node.size, wantConcave);
      frames.insert(frames.end(), chainFrames.begin(), chainFrames.end());
    }

    PolySetBuilder debug(0, 0, 3, /*convex=*/false);
    if (auto edges = debugEdgeMarkers(m, adj, thresholdDeg)) debug.appendPolySet(*edges);
    if (auto spine = debugSpineMarkers(m, frames)) debug.appendPolySet(*spine);
    if (debug.isEmpty()) return nullptr;
    return debug.build();
  }

  const bool isWedgeOnly = type == FilletType::CHAMFER || type == FilletType::BEVEL;

  if (!(node.size > 0)) {
    LOG(message_group::Warning, node.modinst->location(), "", "%1$s: %2$s must be positive",
        node.name(), isWedgeOnly ? "setback" : "radius");
    return nullptr;
  }

  // An empty tool is byte-for-byte what an operator that did nothing at all
  // would return, so selecting nothing has to be said out loud rather than left
  // for the user to infer from a model that did not change. Two ways to get
  // here, and the message separates them: a target with no crease sharp enough
  // at this tessellation, and — far more likely — a tool of the wrong sign,
  // since the edges the other one takes are right there in the count.
  if (selected == 0) {
    const size_t others = wantConcave ? c.featureConvex : c.featureConcave;
    const char *sibling = type == FilletType::FILLET    ? "round_tool"
                          : type == FilletType::CHAMFER ? "bevel_tool"
                          : type == FilletType::ROUND   ? "fillet_tool"
                                                        : "chamfer_tool";
    // fillet() builds both signs from the one target, so "no concave edge" on a
    // convex model is its ordinary case and says nothing worth hearing — the
    // other half is doing the work. Two things are still worth hearing, and the
    // second is the reason this is not simply silence: a half left to work alone
    // is the whole operator, so when the creases all turn the other way that
    // node does nothing at all, and the switch that would fix it is named.
    if (node.type == FilletType::APPLY) {
      // Exactly one half enabled; only that half reaches here, so this cannot
      // fire twice.
      const bool alone = node.inner != node.outer;
      const bool speaker = node.inner ? wantConcave : !wantConcave;
      if (alone && others > 0)
        LOG(message_group::Warning, node.modinst->location(), "",
            "%1$s: no %2$s edge of the target turns more than %3$.1f deg; nothing is built. "
            "The target has %4$d %5$s edge(s) - %6$s = true takes those.",
            node.name(), wantConcave ? "concave" : "convex", thresholdDeg, static_cast<int>(others),
            wantConcave ? "convex" : "concave", wantConcave ? "outer" : "inner");
      else if (c.feature == 0 && speaker)
        LOG(message_group::Warning, node.modinst->location(), "",
            "%1$s: no edge of the target turns more than %2$.1f deg; nothing is built. "
            "min_angle= on a tool node lowers that threshold if the feature is shallower "
            "than the tessellation it was built at.",
            node.name(), thresholdDeg);
    } else if (others > 0)
      LOG(message_group::Warning, node.modinst->location(), "",
          "%1$s: no %2$s edge of the target turns more than %3$.1f deg; nothing is built. "
          "The target has %4$d %5$s edge(s) - %6$s takes those.",
          node.name(), wantConcave ? "concave" : "convex", thresholdDeg, static_cast<int>(others),
          wantConcave ? "convex" : "concave", sibling);
    else
      LOG(message_group::Warning, node.modinst->location(), "",
          "%1$s: no edge of the target turns more than %2$.1f deg; nothing is built. "
          "min_angle= lowers that threshold if the feature is shallower than the "
          "tessellation it was built at.",
          node.name(), thresholdDeg);
    return nullptr;
  }

  // The brushes narrow what is built to the stretches of crease they cover. They
  // are asked about the spine rather than about the tool's volume, so what comes
  // back is a set of parameter intervals and the bead ends on a flat cap square
  // to the crease, at a fixed physical point that does not move when the target
  // is retessellated.
  //
  // This comes before the size gate, so that the gate is asked about the bead
  // that is actually going to be built. Whether the blend still meets the
  // surface it is meant to meet is a question about a place on the crease, and
  // selecting half of one genuinely does change the answer: the half that runs
  // off the end of its face is not being built, and refusing the half that fits
  // — with a warning naming a crease the user never picked out — is refusing
  // work nobody asked for.
  std::vector<Chain> usable = chains;
  // The corners a brush reached without covering. They get no cell, the same way
  // they get none of the stretches that ran into them.
  std::set<int> noCorner;
  if (brush && !brush->isEmpty()) {
    const BrushVolume volume(brush->getManifold().GetMeshGL64());
    // A brush face nearly tangent to the spine crosses it twice a hair apart.
    // The stub that would leave is never intentional, and a hundredth of the
    // size is far below any bead a user would ask for by hand. It is a debounce
    // and nothing else: it applies only where the brush cut the stretch it is
    // measuring, never to a crease selected end to end, whose length is the
    // model's business and not the brush's. Nothing else rests on the value —
    // what makes the one-edge-only selection of the documentation reachable is
    // dropUncoveredCorners below, which drops the neighbouring stubs whatever
    // this is set to.
    const double debounce = 0.01 * node.size;

    std::vector<Chain> selected;
    size_t candidates = 0;
    for (Chain& chain : usable) {
      const size_t segments =
        chain.verts.size() < 2 ? 0 : (chain.closed ? chain.verts.size() : chain.verts.size() - 1);
      candidates += segments;

      std::vector<SpineInterval> keep = chainSelection(m, chain, volume, debounce);
      if (keep.empty()) continue;
      // A chain the brush covers whole carries no intervals at all, which is
      // both cheaper downstream and exactly the unbrushed path.
      if (keep.size() == 1 && keep.front().first <= 0.0 &&
          keep.front().second >= static_cast<double>(segments))
        keep.clear();
      chain.keep = std::move(keep);
      selected.push_back(std::move(chain));
    }

    // A corner the selection arrives at without covering the stretch a corner
    // cell occupies gets neither the cell nor the stretches that would have met
    // in it. What comes back is the corners where that took everything, which is
    // the one case where a brush drawn around a corner is answered with nothing:
    // the neighbour stubs a brush along one edge clips off its corners go the
    // same way and are silent, since that is the rule working. The wedge tools
    // build no corner cell, so nothing there overshoots and nothing is dropped.
    const std::vector<int> uncovered =
      isWedgeOnly ? std::vector<int>{}
                  : dropUncoveredCorners(m, selected, chains, node.size, &noCorner);
    for (const int v : uncovered)
      LOG(message_group::Warning, node.modinst->location(), "",
          "%1$s: the brush covers less than the radius %2$g of the creases meeting at the corner "
          "[%3$.4g, %4$.4g, %5$.4g]; nothing is built there. A corner cell is the full size "
          "whatever is selected, so a corner is built only where the brush reaches the radius down "
          "every edge of it, and a bead stopping short of one would meet nothing.",
          node.name(), node.size, m.pos[v].x(), m.pos[v].y(), m.pos[v].z());

    // Counted per edge, since that is the unit the user wrote the model in; an
    // edge the brush takes any part of counts as taken. Counted after the drop
    // above, so the number is what will be built and not what was proposed.
    size_t taken = 0;
    for (const Chain& chain : selected) {
      const size_t segments =
        chain.verts.size() < 2 ? 0 : (chain.closed ? chain.verts.size() : chain.verts.size() - 1);
      if (chain.keep.empty()) {
        taken += segments;
        continue;
      }
      for (size_t i = 0; i < segments; ++i)
        for (const SpineInterval& iv : chain.keep)
          if (std::min(iv.second, static_cast<double>(i) + 1.0) -
                std::max(iv.first, static_cast<double>(i)) >
              1e-12) {
            ++taken;
            break;
          }
    }

    // Only blame the brush when there was something for it to miss. A target
    // with no crease of this sign has already said so and returned, so what is
    // left here is a brush that really did cover none of them — unless the
    // corners above took the last of it, which has already been reported and in
    // more detail than "the brush covers none".
    if (selected.empty() && candidates > 0 && uncovered.empty())
      LOG(message_group::Warning, node.modinst->location(), "",
          "%1$s: the selection brush covers none of the %2$d candidate edge(s); nothing is built. "
          "The brush has to contain part of an edge, not merely touch the model.",
          node.name(), static_cast<int>(candidates));
    else if (!selected.empty())
      LOG(message_group::Echo, node.modinst->location(), "",
          "%1$s: selection brush takes %2$d of %3$d candidate edge(s)", node.name(),
          static_cast<int>(taken), static_cast<int>(candidates));

    usable = std::move(selected);
  }

  // A size the feature cannot carry is refused, one crease at a time, and never
  // quietly resized: a clamp would have to be agreed with every crease this one
  // meets, and following that through runs a minimum over the whole connected
  // network, so one tight corner would shrink a fillet on the far side of the
  // part where nobody is looking.
  const std::vector<SizeVerdict> verdicts =
    checkChainSizes(m, adj, usable, node.size, wantConcave, isWedgeOnly, thresholdDeg);
  std::vector<Chain> fitting;
  size_t refused = 0;
  const SizeVerdict *worst = nullptr;
  for (size_t ci = 0; ci < usable.size(); ++ci) {
    const SizeVerdict& verdict = verdicts[ci];
    if (verdict.fault == SizeFault::Fits) {
      fitting.push_back(std::move(usable[ci]));
      continue;
    }
    ++refused;
    // The one worth naming is the one that misses by the most: it is the crease
    // to look at first, and on a target with many it is the one whose size the
    // caller most likely meant to ask about.
    if (!worst || verdict.amount > worst->amount) worst = &verdict;
  }
  // One line however many creases went, because a crease is refused per crease
  // and read per model. A target that refuses one is a size to reconsider; a
  // target that refuses thirty is the same message thirty times, and the count
  // is the part that was not already obvious.
  if (worst) {
    const std::string why =
      worst->fault == SizeFault::OffFace
        ? STR("the blend would leave the surface it is meant to meet, by ", worst->amount)
        : STR("another feature ", worst->amount, " away needs the same material");
    if (refused == 1)
      LOG(message_group::Warning, node.modinst->location(), "",
          "%1$s: %2$s %3$g does not fit the crease at [%4$.4g, %5$.4g, %6$.4g] - %7$s. "
          "That crease is dropped; the size is never clamped to make it fit.",
          node.name(), isWedgeOnly ? "setback" : "radius", node.size, worst->where.x(),
          worst->where.y(), worst->where.z(), why);
    else
      LOG(message_group::Warning, node.modinst->location(), "",
          "%1$s: %2$s %3$g does not fit %4$d of the %5$d crease(s) selected; the worst is at "
          "[%6$.4g, %7$.4g, %8$.4g] - %9$s. Those creases are dropped; the size is never clamped "
          "to make it fit.",
          node.name(), isWedgeOnly ? "setback" : "radius", node.size, static_cast<int>(refused),
          static_cast<int>(usable.size()), worst->where.x(), worst->where.y(), worst->where.z(),
          why);
  }
  usable = std::move(fitting);

  // A corner the solve refuses — walls too nearly parallel to pin a point down,
  // or a ball seated so far from the vertex that the answer is not a corner of
  // this feature at all — gets no corner cell, and the blends that meet there
  // fade out to the sharp vertex instead. Say so: the shape is valid but it is
  // not the constant-radius blend that was asked for.
  if (!isWedgeOnly)
    for (const Junction& j : chainJunctions(m, adj, usable, node.size, wantConcave, noCorner))
      if (j.ballCentres.empty())
        LOG(message_group::Warning, node.modinst->location(), "",
            "%1$s: no ball of radius %2$g is seated in the corner at [%3$.4g, %4$.4g, %5$.4g]; "
            "the blends there run out to the sharp vertex instead of closing at that size.",
            node.name(), node.size, m.pos[j.vert].x(), m.pos[j.vert].y(), m.pos[j.vert].z());

  // Chamfer and bevel are the wedge alone. The rounded tools are the same wedge
  // with the rolling ball's canal taken back out of it, and with the setback
  // fixed at the ball's tangency points rather than given by the caller.
  manifold::Manifold tool;
  if (isWedgeOnly) {
    tool = buildWedgeSolid(m, adj, usable, node.size, wantConcave, thresholdDeg);
  } else {
    const int arcSegments = node.discretizer.getCircularSegmentCount(node.size).value_or(0);
    tool = buildRoundSolid(m, adj, usable, node.size, wantConcave, arcSegments, thresholdDeg,
                           noCorner);
  }

  if (tool.IsEmpty()) return nullptr;
  return std::make_shared<ManifoldGeometry>(std::move(tool));
}
