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
#pragma once

#include <cstdint>
#include <memory>

#include "core/FilletNode.h"

class Geometry;
class ManifoldGeometry;

// The fillet operator's geometry core: extract the target mesh, rebuild
// edge -> two-face adjacency from the triangle soup, classify each edge
// (concave/convex plus a tessellation-seam filter), walk the selected edges into
// chains, and build the tool solid along them.
//
// `brush` is the union of the node's selection children, or null when it has
// none. It narrows the tool to the stretches of crease it covers; the whole of
// every crease is taken when it is absent.
//
// `type` is which tool to build, and is passed rather than read off the node
// because the fillet() node asks for one of each sign from a single node.
//
// `addedID` names a source surface of the target that a previous pass of this
// same operator put there, and is how fillet() runs its outer half on the solid
// its inner half already blended without rounding that blend's own facets. Zero
// — the default, and what every tool node passes — means the whole target is the
// user's shape and every crease of it is a feature.
//
// Returns the tool solid, or nullptr where there is nothing to build.
std::shared_ptr<const Geometry> buildFilletTool(
  const FilletNode& node, FilletType type, const std::shared_ptr<const ManifoldGeometry>& target,
  const std::shared_ptr<const ManifoldGeometry>& brush = nullptr, uint32_t addedID = 0);
