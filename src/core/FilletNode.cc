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

#include "core/FilletNode.h"

#include <cassert>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include "core/Builtins.h"
#include "core/Children.h"
#include "core/CurveDiscretizer.h"
#include "core/ModuleInstantiation.h"
#include "core/Parameters.h"
#include "core/module.h"

// Child 0 is the target whose edges are analysed; children 1+ are selection
// brushes. The node parses its parameters here; the edge analysis and tool
// construction happen when the geometry is evaluated.
static std::shared_ptr<AbstractNode> builtin_fillet_impl(const ModuleInstantiation *inst,
                                                         Arguments arguments,
                                                         const Children& children, FilletType type)
{
  // FILLET/ROUND take a radius r; CHAMFER/BEVEL take a setback t. Accept both
  // spellings so either name reads naturally, with the type's primary bound to
  // the first positional argument.
  const bool isChamfer = (type == FilletType::CHAMFER || type == FilletType::BEVEL);
  Parameters parameters =
    Parameters::parse(std::move(arguments), inst->location(),
                      isChamfer ? std::vector<std::string>{"t", "r", "min_angle", "debug"}
                                : std::vector<std::string>{"r", "t", "min_angle", "debug"});

  auto node =
    std::make_shared<FilletNode>(inst, type, CurveDiscretizer(parameters, inst->location()));

  const auto& primary = isChamfer ? parameters["t"] : parameters["r"];
  const auto& secondary = isChamfer ? parameters["r"] : parameters["t"];
  if (primary.type() == Value::Type::NUMBER) {
    node->size = primary.toDouble();
  } else if (secondary.type() == Value::Type::NUMBER) {
    node->size = secondary.toDouble();
  }

  if (parameters["min_angle"].type() == Value::Type::NUMBER) {
    node->min_angle = parameters["min_angle"].toDouble();
  }

  if (parameters["debug"].type() == Value::Type::BOOL) {
    node->debug = parameters["debug"].toBool();
  }

  return children.instantiate(node);
}

static std::shared_ptr<AbstractNode> builtin_fillet_tool(const ModuleInstantiation *inst,
                                                        Arguments arguments, const Children& children)
{
  return builtin_fillet_impl(inst, std::move(arguments), children, FilletType::FILLET);
}

static std::shared_ptr<AbstractNode> builtin_round_tool(const ModuleInstantiation *inst,
                                                       Arguments arguments, const Children& children)
{
  return builtin_fillet_impl(inst, std::move(arguments), children, FilletType::ROUND);
}

static std::shared_ptr<AbstractNode> builtin_chamfer_tool(const ModuleInstantiation *inst,
                                                         Arguments arguments, const Children& children)
{
  return builtin_fillet_impl(inst, std::move(arguments), children, FilletType::CHAMFER);
}

static std::shared_ptr<AbstractNode> builtin_bevel_tool(const ModuleInstantiation *inst,
                                                       Arguments arguments, const Children& children)
{
  return builtin_fillet_impl(inst, std::move(arguments), children, FilletType::BEVEL);
}

std::string FilletNode::name() const
{
  switch (this->type) {
  case FilletType::FILLET:  return "fillet_tool"; break;
  case FilletType::ROUND:   return "round_tool"; break;
  case FilletType::CHAMFER: return "chamfer_tool"; break;
  case FilletType::BEVEL:   return "bevel_tool"; break;
  default:                  assert(false);
  }
  return "internal_error";
}

std::string FilletNode::toString() const
{
  std::ostringstream stream;
  const bool isChamfer = (type == FilletType::CHAMFER || type == FilletType::BEVEL);
  stream << this->name() << "(" << this->discretizer << ", " << (isChamfer ? "t = " : "r = ")
         << this->size;
  if (this->min_angle >= 0) stream << ", min_angle = " << this->min_angle;
  if (this->debug) stream << ", debug = true";
  stream << ")";
  return stream.str();
}

void register_builtin_fillet()
{
  Builtins::init("fillet_tool", new BuiltinModule(builtin_fillet_tool),
                 {
                   "fillet_tool(r = number)",
                 });
  Builtins::init("round_tool", new BuiltinModule(builtin_round_tool),
                 {
                   "round_tool(r = number)",
                 });
  Builtins::init("chamfer_tool", new BuiltinModule(builtin_chamfer_tool),
                 {
                   "chamfer_tool(t = number)",
                 });
  Builtins::init("bevel_tool", new BuiltinModule(builtin_bevel_tool),
                 {
                   "bevel_tool(t = number)",
                 });
}
