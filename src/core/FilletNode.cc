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
#include "core/node.h"

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

// The composition every fillet is: grow the inner creases, THEN cut back the
// outer ones. Written out, because the tool nodes are the composable surface and
// anyone wanting a different composition should be able to start from this one:
//
//   module fillet(r = 2, inner = true, outer = true, min_angle = undef) {
//       module blended() {
//           union() {
//               children();
//               if (inner) fillet_tool(r = r, min_angle = min_angle) children();
//           }
//       }
//       difference() {
//           blended() children();
//           if (outer) round_tool(r = r, min_angle = min_angle) blended() children();
//       }
//   }
//
// This node is exactly that, to the byte, and "then" is the load-bearing word in
// it. The round pass is measured against the solid the fillet pass left, because
// a bead that runs out onto a face of the model leaves its end cross-section
// standing in that face, and a pass that never saw the bead leaves that crescent
// as a sharp lip over the outline it rounded beside it.
//
// `blended() children()` at both call sites is the whole trick and is not
// optional: children() INSIDE blended() means blended's own children, so writing
// the calls as a bare blended() and relying on the definition to see the outer
// ones produces nothing at all, silently.
//
// Under $preview the node hands back its child untouched, because the tools cost
// a mesh analysis and two booleans on every keystroke and the fillets are
// usually the last thing being iterated on. disable_preview = false asks for
// them anyway. The four *_tool nodes deliberately do not do this: a tool is a
// solid the user is composing with by hand, and one that vanished under a render
// mode would break every composition built out of it.
static std::shared_ptr<AbstractNode> builtin_fillet(const ModuleInstantiation *inst,
                                                    Arguments arguments, const Children& children)
{
  Parameters parameters =
    Parameters::parse(std::move(arguments), inst->location(),
                      {"r", "inner", "outer", "min_angle", "disable_preview"});

  const bool disable_preview = parameters["disable_preview"].type() == Value::Type::BOOL
                                 ? parameters["disable_preview"].toBool()
                                 : true;
  const bool preview =
    parameters["$preview"].type() == Value::Type::BOOL && parameters["$preview"].toBool();
  if (preview && disable_preview) {
    // Child 0 only. Children 1+ are selection brushes for the tools rather than
    // geometry, and passing them through would put the brush in the model.
    return children.instantiate(std::make_shared<GroupNode>(inst, "fillet"), {0});
  }

  auto node = std::make_shared<FilletNode>(inst, FilletType::APPLY,
                                           CurveDiscretizer(parameters, inst->location()));
  node->disable_preview = disable_preview;

  if (parameters["r"].type() == Value::Type::NUMBER) node->size = parameters["r"].toDouble();
  if (parameters["inner"].type() == Value::Type::BOOL) node->inner = parameters["inner"].toBool();
  if (parameters["outer"].type() == Value::Type::BOOL) node->outer = parameters["outer"].toBool();
  // Both halves get it. The threshold is a statement about which edges of the
  // target are features, and that cannot depend on which sign is being built.
  if (parameters["min_angle"].type() == Value::Type::NUMBER) {
    node->min_angle = parameters["min_angle"].toDouble();
  }

  return children.instantiate(node);
}

std::string FilletNode::name() const
{
  switch (this->type) {
  case FilletType::FILLET:  return "fillet_tool"; break;
  case FilletType::ROUND:   return "round_tool"; break;
  case FilletType::CHAMFER: return "chamfer_tool"; break;
  case FilletType::BEVEL:   return "bevel_tool"; break;
  case FilletType::APPLY:   return "fillet"; break;
  default:                  assert(false);
  }
  return "internal_error";
}

std::string FilletNode::toString() const
{
  std::ostringstream stream;
  // The wrapper has no setback spelling and no debug overlay, and carries two
  // switches the tools do not, so it prints its own line.
  if (this->type == FilletType::APPLY) {
    stream << this->name() << "(" << this->discretizer << ", r = " << this->size;
    if (!this->inner) stream << ", inner = false";
    if (!this->outer) stream << ", outer = false";
    if (this->min_angle >= 0) stream << ", min_angle = " << this->min_angle;
    if (!this->disable_preview) stream << ", disable_preview = false";
    stream << ")";
    return stream.str();
  }

  const bool isChamfer = (type == FilletType::CHAMFER || type == FilletType::BEVEL);
  stream << this->name() << "(" << this->discretizer << ", " << (isChamfer ? "t = " : "r = ")
         << this->size;
  if (this->min_angle >= 0) stream << ", min_angle = " << this->min_angle;
  if (this->debug) stream << ", debug = true";
  stream << ")";
  return stream.str();
}

// Without Manifold there is no tool builder at all, so the modules are left
// unregistered rather than registered and inert: an unregistered module is an
// unknown-module error naming the line, where a module that evaluates to no
// geometry silently deletes the model fillet() was applied to.
#ifdef ENABLE_MANIFOLD
void register_builtin_fillet()
{
  Builtins::init("fillet_tool", new BuiltinModule(builtin_fillet_tool, &Feature::ExperimentalFillet),
                 {
                   "fillet_tool(r = number)",
                 });
  Builtins::init("round_tool", new BuiltinModule(builtin_round_tool, &Feature::ExperimentalFillet),
                 {
                   "round_tool(r = number)",
                 });
  Builtins::init("chamfer_tool",
                 new BuiltinModule(builtin_chamfer_tool, &Feature::ExperimentalFillet),
                 {
                   "chamfer_tool(t = number)",
                 });
  Builtins::init("bevel_tool", new BuiltinModule(builtin_bevel_tool, &Feature::ExperimentalFillet),
                 {
                   "bevel_tool(t = number)",
                 });
  Builtins::init("fillet", new BuiltinModule(builtin_fillet, &Feature::ExperimentalFillet),
                 {
                   "fillet(r = number)",
                   "fillet(r = number, inner = bool, outer = bool, min_angle = number, "
                   "disable_preview = bool)",
                 });
}
#else
void register_builtin_fillet() {}
#endif  // ENABLE_MANIFOLD
