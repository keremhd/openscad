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
    if (ec.dihedralDeg < thresholdDeg) continue;

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
    if (ec.dihedralDeg < thresholdDeg) continue;
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

std::vector<StationNormals> chainNormals(const MergedMesh& m,
                                         const std::map<EdgeKey, std::vector<int>>& adj,
                                         const Chain& chain)
{
  auto edgeKey = [](int a, int b) { return EdgeKey{std::min(a, b), std::max(a, b)}; };

  // The two wall normals of edge a->b, oriented so side A/B is consistent along a
  // consistently-walked chain (fixed handedness relative to the walk direction).
  auto sidedNormals = [&](int a, int b, Vector3d& nA, Vector3d& nB) {
    const auto it = adj.find(edgeKey(a, b));
    if (it == adj.end() || it->second.size() != 2) return false;
    nA = m.tris[it->second[0]].normal;
    nB = m.tris[it->second[1]].normal;
    Vector3d d = m.pos[b] - m.pos[a];
    const double len = d.norm();
    if (len < 1e-12) return false;
    d /= len;
    if (nA.cross(nB).dot(d) < 0) std::swap(nA, nB);
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
    if (prevV >= 0 && sidedNormals(prevV, v, nA, nB)) { sumA += nA; sumB += nB; }
    if (nextV >= 0 && sidedNormals(v, nextV, nA, nB)) { sumA += nA; sumB += nB; }
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

namespace {

// The wedge cross-section shared by every tool: the two setback points, then the
// same corner pushed a hair past each wall so the tool crosses it transversally
// rather than lying coplanar with it. Each primed point is displaced along its
// *own* wall normal, which is what gives eps its slack — at a crease the
// material is the union of two half-spaces, so a point only has to be behind one
// of them. `dir` is +1 for a concave tool and -1 for a convex one.
std::array<Vector3d, 5> pentagonSection(const Vector3d& v, const Vector3d& nA, const Vector3d& nB,
                                        const Vector3d& bis, const Vector3d& TA,
                                        const Vector3d& TB, double dir, double eps)
{
  return {TA, TB, TB - dir * eps * nB, v - dir * eps * bis, TA - dir * eps * nA};
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
template <typename Section, typename PointsOf>
void appendChainCells(const Chain& chain, const std::vector<Section>& sections,
                      const PointsOf& pointsOf, std::vector<manifold::Manifold>& cells)
{
  const size_t n = sections.size();
  if (n < 2) return;

  const size_t segments = chain.closed ? n : n - 1;
  for (size_t i = 0; i < segments; ++i) {
    const Section& a = sections[i];
    const Section& b = sections[(i + 1) % n];
    if (!a.valid || !b.valid) continue;

    std::vector<manifold::vec3> pts;
    for (const Section *section : {&a, &b})
      for (const Vector3d& p : pointsOf(*section)) pts.emplace_back(p.x(), p.y(), p.z());

    manifold::Manifold cell = manifold::Manifold::Hull(pts);
    if (cell.IsEmpty()) continue;
    cells.push_back(std::move(cell));
  }
}

}  // namespace

std::vector<WedgeSection> wedgeSections(const MergedMesh& m,
                                        const std::map<EdgeKey, std::vector<int>>& adj,
                                        const Chain& chain, double t, bool concave)
{
  const std::vector<StationNormals> stations = chainNormals(m, adj, chain);

  // How far past each wall the tool reaches. Relative to the setback so it
  // scales with the feature, with a floor so a degenerate t still separates the
  // faces by more than the boolean kernel's own tolerance.
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
    w.p = pentagonSection(s.v, s.nA, s.nB, bis, TA, TB, dir, eps);
    w.valid = true;
    out[i] = w;
  }
  return out;
}

manifold::Manifold buildWedgeSolid(const MergedMesh& m,
                                   const std::map<EdgeKey, std::vector<int>>& adj,
                                   const std::vector<Chain>& chains, double t, bool concave)
{
  if (!(t > 0)) return {};

  std::vector<manifold::Manifold> cells;
  for (const Chain& chain : chains)
    appendChainCells(chain, wedgeSections(m, adj, chain, t, concave),
                     [](const WedgeSection& s) -> const std::array<Vector3d, 5>& { return s.p; },
                     cells);

  return unionCells(cells);
}

std::vector<RoundSection> roundSections(const MergedMesh& m,
                                        const std::map<EdgeKey, std::vector<int>>& adj,
                                        const Chain& chain, double r, bool concave,
                                        int arcSegments)
{
  const std::vector<SpineFrame> frames = spineFrames(m, adj, chain, r, concave);

  const double eps = std::max(1e-3 * std::abs(r), 1e-9);
  const double dir = concave ? 1.0 : -1.0;
  // Three points is the coarsest thing that still bounds an area; a radius too
  // small for the discretizer to have an opinion about lands here.
  const int segs = std::max(arcSegments, 3);

  std::vector<RoundSection> out(frames.size());
  for (size_t i = 0; i < frames.size(); ++i) {
    const SpineFrame& f = frames[i];
    if (!f.valid) continue;

    Vector3d bis = f.nA + f.nB;
    if (bis.norm() < 1e-9) continue;
    bis.normalize();

    // The plane of the section is the one spanned by the two wall normals: C, v,
    // TA and TB all lie in it by construction, so the arc meets each wall
    // tangentially there whatever the spine does between stations. Taking it
    // from the normals rather than from the spine direction is what keeps that
    // true around a bend, where the two disagree.
    Vector3d e = f.nA.cross(f.nB);
    if (e.norm() < 1e-12) continue;
    e.normalize();
    const Vector3d e1 = f.nA;  // unit, and perpendicular to e
    const Vector3d e2 = e.cross(e1);

    RoundSection s;
    s.w = pentagonSection(f.v, f.nA, f.nB, bis, f.TA, f.TB, dir, eps);

    s.u.reserve(segs);
    for (int k = 0; k < segs; ++k) {
      const double a = 2.0 * M_PI * k / segs;
      s.u.push_back(f.C + r * (std::cos(a) * e1 + std::sin(a) * e2));
    }

    s.valid = true;
    out[i] = std::move(s);
  }
  return out;
}

manifold::Manifold buildRoundSolid(const MergedMesh& m,
                                   const std::map<EdgeKey, std::vector<int>>& adj,
                                   const std::vector<Chain>& chains, double r, bool concave,
                                   int arcSegments)
{
  if (!(r > 0)) return {};

  std::vector<manifold::Manifold> beads;
  for (const Chain& chain : chains) {
    const std::vector<RoundSection> sections =
      roundSections(m, adj, chain, r, concave, arcSegments);

    std::vector<manifold::Manifold> wedgeCells, canalCells;
    appendChainCells(chain, sections,
                     [](const RoundSection& s) -> const std::array<Vector3d, 5>& { return s.w; },
                     wedgeCells);
    appendChainCells(chain, sections,
                     [](const RoundSection& s) -> const std::vector<Vector3d>& { return s.u; },
                     canalCells);

    manifold::Manifold wedge = unionCells(wedgeCells);
    if (wedge.IsEmpty()) continue;

    // Subtract once per chain, over the whole of its wedge rather than cell by
    // cell: where the spine bends, one cell's ball reaches into the next cell's
    // wedge and has to cut it. But not once over every chain together — a ball
    // rolling along one crease would then hollow out the neighbouring crease's
    // bead where the two meet, leaving a lump on the model at every corner. What
    // belongs at a meeting point is a corner cell, not a hole.
    const manifold::Manifold canal = unionCells(canalCells);
    beads.push_back(canal.IsEmpty() ? std::move(wedge)
                                    : dropVolumelessParts(wedge - canal));
  }

  return unionCells(beads);
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
    const Color4f color = ec.dihedralDeg < thresholdDeg ? seamColor
                          : ec.concave                  ? concaveColor
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
// into chains, and build the tool solid along them. Junction cells are not built
// yet, so chains meeting at a branch vertex simply overlap there. A diagnostic
// count line goes out on every invocation (a plain cube yields 12 feature edges,
// all convex; an inside corner yields a single concave edge).
std::shared_ptr<const Geometry> buildFilletTool(
  const FilletNode& node, const std::shared_ptr<const ManifoldGeometry>& target)
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
  const bool wantConcave =
    node.type == FilletType::FILLET || node.type == FilletType::CHAMFER;

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

  const bool isWedgeOnly =
    node.type == FilletType::CHAMFER || node.type == FilletType::BEVEL;

  if (!(node.size > 0)) {
    LOG(message_group::Warning, node.modinst->location(), "", "%1$s: %2$s must be positive",
        node.name(), isWedgeOnly ? "setback" : "radius");
    return nullptr;
  }

  // Chamfer and bevel are the wedge alone. The rounded tools are the same wedge
  // with the rolling ball's canal taken back out of it, and with the setback
  // fixed at the ball's tangency points rather than given by the caller.
  manifold::Manifold tool;
  if (isWedgeOnly) {
    tool = buildWedgeSolid(m, adj, chains, node.size, wantConcave);
  } else {
    const int arcSegments = node.discretizer.getCircularSegmentCount(node.size).value_or(0);
    tool = buildRoundSolid(m, adj, chains, node.size, wantConcave, arcSegments);
  }

  if (tool.IsEmpty()) return nullptr;
  return std::make_shared<ManifoldGeometry>(std::move(tool));
}
