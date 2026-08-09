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

// The fillet mesh-classification core: reduce a MeshGL to topological form
// (mergeMesh), rebuild edge -> two-face adjacency (buildEdgeAdjacency), classify
// each two-face edge as concave/convex feature or seam (classifyEdge,
// classifyEdges), and group triangles into smooth surfaces (smoothSurfaces).
// buildBlend is the only caller; the declarations live in the internal header so
// the unit test can exercise the combinatorics directly.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include <manifold/manifold.h>

#include "geometry/fillet/FilletMesh_internal.h"
#include "geometry/linalg.h"

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
  int aFar = -1;
  for (const int k : A.v)
    if (k != key.first && k != key.second) {
      aFar = k;
      break;
    }
  // A has no third vertex, so it is degenerate and its normal means nothing.
  // A negative dihedral is below every threshold, which keeps it out of the
  // feature edges rather than letting it answer "convex" by default.
  if (aFar < 0) return {-1.0, false};
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

}  // namespace fillet::detail
