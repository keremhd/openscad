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

#include <string>
#include <utility>

#include "core/CurveDiscretizer.h"
#include "core/ModuleInstantiation.h"
#include "core/node.h"
#include "geometry/linalg.h"

// The four fillet/round/chamfer/bevel tool nodes. Convexity sign is baked into
// the node name: FILLET/CHAMFER are concave tools the caller unions; ROUND/BEVEL
// are convex tools the caller subtracts.
enum class FilletType { FILLET, ROUND, CHAMFER, BEVEL };

class FilletNode : public AbstractNode
{
public:
  VISITABLE();
  FilletNode(const ModuleInstantiation *mi, FilletType type, CurveDiscretizer discretizer)
    : AbstractNode(mi), type(type), discretizer(std::move(discretizer)) {}
  std::string toString() const override;
  std::string name() const override;

  // radius (FILLET/ROUND) or setback (CHAMFER/BEVEL)
  double size{0.0};
  // override for the auto-derived dihedral threshold; <0 means "auto"
  double min_angle{-1.0};
  FilletType type;
  CurveDiscretizer discretizer;
};
