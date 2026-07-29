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

#include <memory>

class Geometry;
class ManifoldGeometry;
class FilletNode;

// The fillet operator's geometry core: extract the target mesh, rebuild
// edge -> two-face adjacency from the triangle soup, classify each edge
// (concave/convex plus a tessellation-seam filter), walk the selected edges into
// chains, and build the tool solid along them.
//
// `brush` is the union of the node's selection children, or null when it has
// none. It narrows the tool to the stretches of crease it covers; the whole of
// every crease is taken when it is absent.
//
// Returns the tool solid, or nullptr where there is nothing to build.
std::shared_ptr<const Geometry> buildFilletTool(
  const FilletNode& node, const std::shared_ptr<const ManifoldGeometry>& target,
  const std::shared_ptr<const ManifoldGeometry>& brush = nullptr);
