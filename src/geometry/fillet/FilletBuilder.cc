/*
 *  OpenSCAD (www.openscad.org)
 *  Copyright (C) 2009-2011 Clifford Wolf <clifford@clifford.at> and
 *                          Marius Kintel <marius@kintel.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  As a special exception, you have permission to link this program
 *  with the CGAL library and distribute executables, as long as you
 *  follow the requirements of the GNU GPL in regard to all of the
 *  software in the executable aside from CGAL.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "geometry/fillet/FilletBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include <manifold/manifold.h>

#include "core/FilletNode.h"
#include "geometry/Geometry.h"
#include "geometry/linalg.h"
#include "geometry/manifold/ManifoldGeometry.h"
#include "utils/printutils.h"

namespace {

// One mesh triangle after MeshGL's per-run vertex duplicates have been merged
// by position: the three topological (merged) vertex indices, the outward face
// normal, and the source-surface id Manifold propagates through booleans.
struct Tri
{
  std::array<int, 3> v;
  Vector3d normal;
  uint32_t originalID;
};

// Undirected edge, endpoints stored min-first so both winding directions land
// on the same key.
using EdgeKey = std::pair<int, int>;

}  // namespace

// Rebuild edge -> two-face adjacency from the target's triangle soup and
// classify each edge as concave/convex and feature/seam. The classified edges
// are the input to spine construction and tool building; for now the pass only
// reports diagnostic counts (e.g. a plain cube yields 12 feature edges, all
// convex; an inside corner yields a single concave edge) and emits no geometry.
std::shared_ptr<const Geometry> buildFilletTool(
  const FilletNode& node, const std::shared_ptr<const ManifoldGeometry>& target)
{
  if (!target || target->isEmpty()) {
    LOG(message_group::Warning, node.modinst->location(), "",
        "%1$s: target has no geometry to fillet", node.name());
    return nullptr;
  }

  const manifold::MeshGL64 mesh = target->getManifold().GetMeshGL64();
  const size_t numProp = mesh.numProp;
  const size_t numRawVert = numProp ? mesh.vertProperties.size() / numProp : 0;
  const size_t numTri = mesh.triVerts.size() / 3;

  // Merge MeshGL vertices by exact position. A single spatial vertex is emitted
  // once per run it touches (runs differ by surface id), so the raw indices do
  // not give topological adjacency until coincident positions are unified.
  std::map<std::array<double, 3>, int> canonOf;
  std::vector<Vector3d> pos;  // indexed by merged id
  std::vector<int> canon(numRawVert);
  for (size_t i = 0; i < numRawVert; ++i) {
    const double x = mesh.vertProperties[i * numProp + 0];
    const double y = mesh.vertProperties[i * numProp + 1];
    const double z = mesh.vertProperties[i * numProp + 2];
    const std::array<double, 3> key{x, y, z};
    auto [it, inserted] = canonOf.try_emplace(key, static_cast<int>(pos.size()));
    if (inserted) pos.emplace_back(x, y, z);
    canon[i] = it->second;
  }

  // Build triangles with merged indices, outward normals, and per-triangle
  // source id (walk the runs exactly as ManifoldGeometry::toPolySet does).
  std::vector<Tri> tris;
  tris.reserve(numTri);
  std::set<uint32_t> distinctIDs;
  size_t run = 0;
  for (size_t t = 0; t < numTri; ++t) {
    const size_t base = t * 3;
    while (run + 1 < mesh.runIndex.size() && base >= mesh.runIndex[run + 1]) ++run;
    const uint32_t id = run < mesh.runOriginalID.size() ? mesh.runOriginalID[run] : 0;
    distinctIDs.insert(id);

    const int a = canon[mesh.triVerts[base + 0]];
    const int b = canon[mesh.triVerts[base + 1]];
    const int c = canon[mesh.triVerts[base + 2]];
    Vector3d n = (pos[b] - pos[a]).cross(pos[c] - pos[a]);
    const double len = n.norm();
    if (len > 0) n /= len;
    tris.push_back({{a, b, c}, n, id});
  }

  // Edge -> incident triangles. A manifold mesh gives exactly two per edge.
  std::map<EdgeKey, std::vector<int>> edgeTris;
  for (size_t t = 0; t < tris.size(); ++t) {
    const auto& tv = tris[t].v;
    for (int e = 0; e < 3; ++e) {
      const int u = tv[e], w = tv[(e + 1) % 3];
      edgeTris[{std::min(u, w), std::max(u, w)}].push_back(static_cast<int>(t));
    }
  }

  // Derive the crease threshold from the tessellation parameters so that arc
  // seams the discretizer itself produces (and any the primitive produced) fall
  // below it and are not mistaken for feature edges. min_angle= overrides.
  const double thresholdDeg =
    node.min_angle >= 0 ? node.min_angle : 1.5 * node.discretizer.getMaxSeamAngle();
  // Face provenance (whether two faces trace back to the same source surface)
  // is only meaningful when the mesh carries more than one source id; a single-
  // id mesh (imported STL, polyhedron) can't rely on it.
  const bool useProvenance = distinctIDs.size() > 1;
  const bool wantConcave =
    node.type == FilletType::FILLET || node.type == FilletType::CHAMFER;

  size_t numTwoFace = 0, numNonManifold = 0;
  size_t feature = 0, featureConcave = 0, featureConvex = 0, featureSameSurface = 0;
  for (const auto& [key, ts] : edgeTris) {
    if (ts.size() != 2) {
      ++numNonManifold;
      continue;
    }
    ++numTwoFace;
    const Tri& A = tris[ts[0]];
    const Tri& B = tris[ts[1]];

    double d = A.normal.dot(B.normal);
    d = std::clamp(d, -1.0, 1.0);
    const double phi = std::acos(d) * 180.0 / M_PI;

    // The angle threshold is the primary filter: keep only creases sharper than
    // any seam the tessellation can produce. Provenance is reported alongside
    // rather than used to override — same-id yet sharp edges are real on hard-
    // edged primitives (a cube's own corners), so it must not silently drop them.
    if (phi < thresholdDeg) continue;

    // dot(nA, nB) alone cannot tell an inner corner from an outer one (both give
    // the same angle). Ask whether A's far corner pokes in front of B's plane.
    int aFar = A.v[0];
    for (const int k : A.v)
      if (k != key.first && k != key.second) {
        aFar = k;
        break;
      }
    const bool concave = B.normal.dot(pos[aFar] - pos[key.first]) > 0;

    ++feature;
    if (concave) ++featureConcave; else ++featureConvex;
    if (useProvenance && A.originalID == B.originalID) ++featureSameSurface;
  }

  const size_t selected = wantConcave ? featureConcave : featureConvex;
  LOG(message_group::Echo, node.modinst->location(), "",
      "%1$s: mesh %2$d verts (%3$d merged), %4$d tris, %5$d surfaces; "
      "%6$d edges (%7$d two-face, %8$d non-manifold); feature edges %9$d "
      "(concave %10$d, convex %11$d, %12$d same-surface); selects %13$d %14$s "
      "edge(s) at %15$.1f deg",
      node.name(), static_cast<int>(numRawVert), static_cast<int>(pos.size()),
      static_cast<int>(numTri), static_cast<int>(distinctIDs.size()),
      static_cast<int>(edgeTris.size()), static_cast<int>(numTwoFace),
      static_cast<int>(numNonManifold), static_cast<int>(feature),
      static_cast<int>(featureConcave), static_cast<int>(featureConvex),
      static_cast<int>(featureSameSurface), static_cast<int>(selected),
      wantConcave ? "concave" : "convex", thresholdDeg);

  // No geometry yet; the classified edges will feed spine construction and tool
  // building.
  return nullptr;
}
