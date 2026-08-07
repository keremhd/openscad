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

// The two blending operators. fillet() applies a circular-arc blend, chamfer() a
// flat cut; each consumes its children (child 0 the model, children 1+ selection
// brushes) and returns the finished blended solid, adding material on concave
// edges and removing it on convex ones in one pass. The type is which of the two,
// not a sign — sign is read per edge from the mesh and gated by convex/concave.
//
// (ROUND/BEVEL/APPLY are retained only so the older swept-tool internals still
// compile; the new operators never build those types.)
enum class FilletType { FILLET, ROUND, CHAMFER, BEVEL, APPLY };

class FilletNode : public AbstractNode
{
public:
  VISITABLE();
  FilletNode(const ModuleInstantiation *mi, FilletType type, CurveDiscretizer discretizer)
    : AbstractNode(mi), type(type), discretizer(std::move(discretizer)) {}
  std::string toString() const override;
  std::string name() const override;

  // radius (fillet) or setback (chamfer)
  double size{0.0};
  // override for the auto-derived dihedral threshold; <0 means "auto"
  double min_angle{-1.0};
  // The sign filter: convex keeps ridge edges (rounded/removed), concave
  // keeps valley edges (filled/added). Both default true; both false is a usage
  // error that returns the model unchanged with a warning.
  bool convex{true};
  bool concave{true};

  // --- retained for the swept-tool internals only; unused by the operators ---
  bool debug{false};
  bool inner{true};
  bool outer{true};
  bool disable_preview{true};
  FilletType type;
  CurveDiscretizer discretizer;
};
