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

#include "core/FilletNode.h"

class Geometry;
class ManifoldGeometry;

// The blending core: a direct topological bevel of the target
// mesh. For each selected edge the incident faces are lifted, a blend strip is
// inserted (a circular arc for fillet, a flat cut for chamfer), and the seam is
// reconciled locally against the incident facets — no boolean kernel. Concave
// edges add material, convex edges remove it, in one pass, so the returned solid
// is the finished blend, not a tool to compose.
//
// `target` is the model (child 0). `brush` is the union of the node's selection
// children, or null when it has none, and narrows the blend to the stretches of
// crease it covers.
//
// Returns the finished solid, or the target unchanged where there is nothing to
// build (with a warning naming why).
std::shared_ptr<const Geometry> buildBlend(
  const FilletNode& node, const std::shared_ptr<const ManifoldGeometry>& target,
  const std::shared_ptr<const ManifoldGeometry>& brush = nullptr);
