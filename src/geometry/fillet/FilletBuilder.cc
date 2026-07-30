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
#include <limits>
#include <map>
#include <memory>
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

  return chains;
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
    const Vector3d& a = m.pos[chain.verts[i]];
    const Vector3d& b = m.pos[chain.verts[(i + 1) % n]];
    segmentLength[i] = (b - a).norm();
    for (const auto& c : brush.segmentCrossings(a, b))
      events.push_back({static_cast<double>(i) + c.t, c.entering});
  }
  std::sort(events.begin(), events.end(),
            [](const Event& x, const Event& y) { return x.p < y.p; });

  // A crossing says which way it goes, so the state before the first one is
  // read off it directly. Only a chain that crosses nothing needs the brush
  // asked about a point, and then one point settles the whole chain.
  bool inside = events.empty() ? brush.contains(m.pos[chain.verts[0]]) : !events.front().entering;

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

  const int n = static_cast<int>(chain.verts.size());
  std::vector<StationNormals> out(n);
  for (int i = 0; i < n; ++i) {
    const int v = chain.verts[i];
    StationNormals s;
    s.v = m.pos[v];

    // Average each wall's normal over the vertex's incident chain edges (with
    // wrap-around on a closed ring); an open end has only one incident edge.
    const int prevV = i > 0 ? chain.verts[i - 1] : (chain.closed ? chain.verts[n - 1] : -1);
    const int nextV = i < n - 1 ? chain.verts[i + 1] : (chain.closed ? chain.verts[0] : -1);
    Vector3d sumA = Vector3d::Zero(), sumB = Vector3d::Zero(), nA, nB;
    if (prevV >= 0 && sidedNormals(prevV, v, nA, nB)) {
      sumA += nA;
      sumB += nB;
      sidedTris(m, adj, prevV, v, s.triA, s.triB);
    }
    if (nextV >= 0 && sidedNormals(v, nextV, nA, nB)) {
      sumA += nA;
      sumB += nB;
      if (s.triA < 0) sidedTris(m, adj, v, nextV, s.triA, s.triB);
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
      const int prevV = i > 0 ? chain.verts[i - 1] : (chain.closed ? chain.verts[n - 1] : -1);
      const int nextV = i + 1 < n ? chain.verts[i + 1] : (chain.closed ? chain.verts[0] : -1);
      int inA = -1, inB = -1, outA = -1, outB = -1;
      if (prevV >= 0 && nextV >= 0 && sideSurfaces(prevV, chain.verts[i], inA, inB) &&
          sideSurfaces(chain.verts[i], nextV, outA, outB)) {
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
    if (!sidedTris(m, adj, chain.verts[i], chain.verts[(i + 1) % n], segA, segB)) continue;

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
  for (const Chain& chain : chains)
    contacts.push_back(chainContacts(m, adj, chain, size, concave, wedge, surfaceOf,
                                     /*samplesPerSegment=*/3));

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

  std::vector<std::set<int>> chainVerts(chains.size());
  for (size_t ci = 0; ci < chains.size(); ++ci)
    chainVerts[ci].insert(chains[ci].verts.begin(), chains[ci].verts.end());

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
    const size_t last = contacts[ci].empty() ? 0 : contacts[ci].size() - 1;
    for (size_t i = 0; i < contacts[ci].size(); ++i) {
      const ChainContact& c = contacts[ci][i];
      if (!c.valid) continue;
      if (!chains[ci].closed && (i == 0 || i == last)) continue;
      bool nearJunction = false;
      for (const Vector3d& p : junctionPos)
        if ((c.v - p).norm() < junctionReach) { nearJunction = true; break; }
      if (nearJunction) continue;
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
        for (const int v : chains[cj].verts)
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

manifold::Manifold unionCells(std::vector<manifold::Manifold>& cells)
{
  if (cells.empty()) return {};
  if (cells.size() == 1) return cells.front();
  return dropVolumelessParts(manifold::Manifold::BatchBoolean(cells, manifold::OpType::Add));
}

// Hull each consecutive pair of cross-sections along a chain, appending one cell
// per spine segment. A closed chain wraps, so its last station also pairs with
// its first. Degenerate segments (a zero-length spine step, or a section that
// collapsed) hull to nothing rather than to a bad solid and are dropped.
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

      manifold::Manifold cell = manifold::Manifold::Hull(pts);
      if (cell.IsEmpty()) continue;
      cells.push_back(std::move(cell));
    }
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
    const double seg = (m.pos[chain.verts[j]] - m.pos[chain.verts[i]]).norm();
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
// A junction is built only where every chain meeting there covers it. Two
// reasons, and the second is why touching the vertex is not enough. A corner
// cell closing three beads when only two of them exist is a lump sitting on the
// model rather than a corner. And the cell is a fixed size — it is hulled from
// the seated ball and the sections the beads stop at, and there is no
// perpendicular to clip it against in three directions at once — so a brush that
// reaches a corner by a fraction of the setback still gets the whole of it,
// which is the brush contract broken by however much was missing. Along an edge
// the same brush is honoured to the micron. The decision is therefore binary at
// the setback: cover it and get a corner, fall inside it and get none — and the
// stretch that fell inside is dropped with the corner rather than built as a stub
// meeting nothing, which is what dropUncoveredCorners does to it.
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

  std::vector<manifold::Manifold> cells;
  for (const Chain& chain : chains)
    appendChainCells(chain, wedgeSections(m, adj, chain, t, concave, thresholdDeg),
                     [](const WedgeSection& s) -> const std::array<Vector3d, 5>& { return s.p; },
                     lerpWedge, chain.keep, cells);

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

std::vector<int> dropUncoveredCorners(const MergedMesh& m, std::vector<Chain>& chains, double r)
{
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

  // Three ends is what makes a vertex a corner, and three covered ends is what
  // builds one; a vertex over the first and short of the second is a corner that
  // cannot be had, and everything cut short at it goes.
  std::set<int> uncovered;
  for (const auto& [v, count] : touching)
    if (count >= 3 && covering[v] < 3) uncovered.insert(v);
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
    if (covering[v] == 0) out.push_back(v);

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

std::vector<Junction> chainJunctions(const MergedMesh& m,
                                     const std::map<EdgeKey, std::vector<int>>& adj,
                                     const std::vector<Chain>& chains, double r, bool concave)
{
  // Chains are cut at every vertex whose crease degree is not two, so the number
  // of chain ends landing on a vertex is that degree. A closed ring has no ends
  // and never contributes.
  //
  // An end the brushes cut short of the setback does not count: the corner cell
  // is the full seated ball whatever is selected, so building one for a brush
  // that covers part of the stretch it occupies puts material outside the brush
  // — and one that closes three beads that are not all present is a lump sitting
  // on the model rather than a corner.
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
    if (nbrs.size() < 3) continue;

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
                                   int arcSegments, double thresholdDeg)
{
  if (!(r > 0)) return {};

  const double dir = concave ? 1.0 : -1.0;
  const double eps = std::max(1e-3 * r, 1e-9);
  const int segs = std::max(arcSegments, 3);

  const std::vector<Junction> junctions = chainJunctions(m, adj, chains, r, concave);
  std::map<int, const Junction *> junctionAt;
  for (const Junction& j : junctions) junctionAt[j.vert] = &j;

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
  for (size_t ci = 0; ci < chains.size(); ++ci) {
    sectionAt[ci].resize(sections[ci].size());
    for (size_t i = 0; i < sectionAt[ci].size(); ++i) sectionAt[ci][i] = static_cast<double>(i);
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
  std::vector<std::vector<SpineInterval>> keepOf(chains.size());
  for (size_t ci = 0; ci < chains.size(); ++ci) {
    keepOf[ci] = toSectionSpace(chains[ci].keep, sectionAt[ci], chains[ci].closed);
    if (keepOf[ci].empty() && !chains[ci].keep.empty()) chainUsable[ci] = false;
  }

  std::vector<manifold::Manifold> wedgeCells, canalCells;
  for (size_t ci = 0; ci < chains.size(); ++ci) {
    if (!chainUsable[ci]) continue;
    const size_t n = sections[ci].size();
    const size_t segments = n < 2 ? 0 : (chains[ci].closed ? n : n - 1);
    appendChainCells(chains[ci], sections[ci],
                     [](const RoundSection& s) -> const std::array<Vector3d, 5>& { return s.w; },
                     lerpSection, keepOf[ci], wedgeCells);
    appendChainCells(chains[ci], sections[ci],
                     [](const RoundSection& s) -> const std::vector<Vector3d>& { return s.u; },
                     lerpSection, overhang(keepOf[ci], segments), canalCells);
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
    const double here = epsAt(j);
    // Where the corner cell stands relative to the beads' overshoot and the
    // arc's two: see cornerCell for why it is between them rather than at either.
    const double over = 1.5 * here;
    const double ballPast = ballShort + 2.0 * here;
    std::vector<std::array<Vector3d, 4>> profiles;
    for (const RoundSection& s : endSections[j.vert]) profiles.push_back(cornerProfile(s, over));
    manifold::Manifold cell = cornerCell(j, m.pos[j.vert], profiles, r, dir, over);
    if (cell.IsEmpty()) continue;
    wedgeCells.push_back(std::move(cell));
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
  }

  // One subtraction over everything. The spines stop where the ball does, so the
  // canal is exactly the set of positions the ball can occupy and nothing in it
  // is material another crease still needs — while the corner ball, conversely,
  // has to cut the wedges of every chain meeting at its vertex, which grouping
  // the subtraction per chain would prevent.
  const manifold::Manifold wedge = unionCells(wedgeCells);
  if (wedge.IsEmpty()) return {};
  const manifold::Manifold canal = unionCells(canalCells);
  if (canal.IsEmpty()) return wedge;
  return dropVolumelessParts(wedge - canal);
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
  const std::vector<Chain> chains = buildChains(m, selectedKeys);

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
      isWedgeOnly ? std::vector<int>{} : dropUncoveredCorners(m, selected, node.size);
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
  for (size_t ci = 0; ci < usable.size(); ++ci) {
    const SizeVerdict& verdict = verdicts[ci];
    if (verdict.fault == SizeFault::Fits) {
      fitting.push_back(std::move(usable[ci]));
      continue;
    }
    LOG(message_group::Warning, node.modinst->location(), "",
        "%1$s: %2$s %3$g does not fit the crease at [%4$.4g, %5$.4g, %6$.4g] - %7$s. "
        "That crease is dropped; the size is never clamped to make it fit.",
        node.name(), isWedgeOnly ? "setback" : "radius", node.size, verdict.where.x(),
        verdict.where.y(), verdict.where.z(),
        verdict.fault == SizeFault::OffFace
          ? STR("the blend would leave the surface it is meant to meet, by ", verdict.amount)
          : STR("another feature ", verdict.amount, " away needs the same material"));
  }
  usable = std::move(fitting);

  // A corner the solve refuses — walls too nearly parallel to pin a point down,
  // or a ball seated so far from the vertex that the answer is not a corner of
  // this feature at all — gets no corner cell, and the blends that meet there
  // fade out to the sharp vertex instead. Say so: the shape is valid but it is
  // not the constant-radius blend that was asked for.
  if (!isWedgeOnly)
    for (const Junction& j : chainJunctions(m, adj, usable, node.size, wantConcave))
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
    tool = buildRoundSolid(m, adj, usable, node.size, wantConcave, arcSegments, thresholdDeg);
  }

  if (tool.IsEmpty()) return nullptr;
  return std::make_shared<ManifoldGeometry>(std::move(tool));
}
