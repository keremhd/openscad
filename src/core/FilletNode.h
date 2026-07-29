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

// The four fillet/round/chamfer/bevel tool nodes, plus the fillet() wrapper.
// Convexity sign is baked into the tool names: FILLET/CHAMFER are concave tools
// the caller unions; ROUND/BEVEL are convex tools the caller subtracts. APPLY is
// not a tool at all — it is the node that builds a concave and a convex tool
// from one target and composes both with it.
enum class FilletType { FILLET, ROUND, CHAMFER, BEVEL, APPLY };

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
  // emit the edge/spine diagnostic overlay instead of the tool solid. Off by
  // default: the overlay is a cloud of disjoint marker cubes rather than a
  // solid, so it is for looking at, not for building with.
  bool debug{false};
  // APPLY only: which half to compose. Each switches off its own tool.
  bool inner{true};
  bool outer{true};
  // APPLY only, and recorded rather than acted on: the passthrough this selects
  // happens at instantiation, so a node that exists at all is one that builds.
  // Kept so a dump says which way it was asked.
  bool disable_preview{true};
  FilletType type;
  CurveDiscretizer discretizer;
};
