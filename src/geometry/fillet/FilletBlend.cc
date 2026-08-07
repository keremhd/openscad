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

#include <cstddef>
#include <map>
#include <memory>
#include <vector>

#include <manifold/manifold.h>

#include "core/FilletNode.h"
#include "geometry/Geometry.h"
#include "geometry/fillet/FilletBuilder_internal.h"
#include "geometry/manifold/ManifoldGeometry.h"
#include "utils/printutils.h"

using namespace fillet::detail;

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

  // Both sign flags off is a usage error: the region is selected but no sign is
  // kept, so nothing could be built. Return the model unchanged, loudly — never
  // silently drop it (promise 1).
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

  // Classification is a property of the mesh alone (§4): the crease threshold is a
  // constant unless min_angle= names one, and nothing here reads $fn/$fa/$fs.
  const MergedMesh m = mergeMesh(target->getManifold().GetMeshGL64());
  const std::map<EdgeKey, std::vector<int>> adj = buildEdgeAdjacency(m.tris);
  const double thresholdDeg = node.min_angle >= 0 ? node.min_angle : kDefaultCreaseThresholdDeg;
  const bool useProvenance = m.distinctIDs.size() > 1;
  const ClassCounts c = classifyEdges(m, adj, thresholdDeg, useProvenance);

  // The edges this call will act on: feature edges whose sign passes the filter.
  const std::size_t wantConcave = node.concave ? c.featureConcave : 0;
  const std::size_t wantConvex = node.convex ? c.featureConvex : 0;
  const std::size_t selected = wantConcave + wantConvex;

  if (selected == 0) {
    LOG(message_group::Warning, node.modinst->location(), "",
        "%1$s: no selected edge turns more than %2$.1f deg; the model is returned unchanged "
        "(min_angle= lowers that threshold)",
        node.name(), thresholdDeg);
    return target;
  }

  // TODO(step 2): build the per-edge blend strips + local seam reconciliation.
  // Until then the operator is a documented no-op: it classifies and reports, and
  // returns the model unchanged (matching or improving on today's valid-but-wrong
  // behaviour, judged on vertex count against a plain render).
  LOG(message_group::Echo, node.modinst->location(), "",
      "%1$s: %2$s %3$g selects %4$d edge(s) (%5$d concave, %6$d convex) at %7$.1f deg "
      "[skeleton: geometry not yet built]",
      node.name(), sizeName, node.size, static_cast<int>(selected),
      static_cast<int>(wantConcave), static_cast<int>(wantConvex), thresholdDeg);
  return target;
}
