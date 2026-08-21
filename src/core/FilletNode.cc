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

// Both operators: child 0 is the model, children 1+ are selection brushes. Each
// consumes its children and returns the finished blended solid, built at geometry
// evaluation. `type` is FILLET (arc) or CHAMFER (flat cut); the per-edge sign is
// read from the mesh and gated by convex=/concave=.
static std::shared_ptr<AbstractNode> builtin_blend_impl(const ModuleInstantiation *inst,
                                                        Arguments arguments,
                                                        const Children& children, FilletType type)
{
  // fillet takes a radius r, chamfer a setback t. The primary name binds to the
  // first positional argument; the other spelling is accepted for convenience.
  const bool isChamfer = (type == FilletType::CHAMFER);
  Parameters parameters = Parameters::parse(
    std::move(arguments), inst->location(),
    isChamfer ? std::vector<std::string>{"t", "r", "min_angle", "convex", "concave"}
              : std::vector<std::string>{"r", "t", "min_angle", "convex", "concave"});

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
  if (parameters["convex"].type() == Value::Type::BOOL) node->convex = parameters["convex"].toBool();
  if (parameters["concave"].type() == Value::Type::BOOL)
    node->concave = parameters["concave"].toBool();

  return children.instantiate(node);
}

static std::shared_ptr<AbstractNode> builtin_fillet(const ModuleInstantiation *inst,
                                                    Arguments arguments, const Children& children)
{
  return builtin_blend_impl(inst, std::move(arguments), children, FilletType::FILLET);
}

static std::shared_ptr<AbstractNode> builtin_chamfer(const ModuleInstantiation *inst,
                                                     Arguments arguments, const Children& children)
{
  return builtin_blend_impl(inst, std::move(arguments), children, FilletType::CHAMFER);
}

std::string FilletNode::name() const
{
  return this->type == FilletType::CHAMFER ? "chamfer" : "fillet";
}

std::string FilletNode::toString() const
{
  std::ostringstream stream;
  const bool isChamfer = (this->type == FilletType::CHAMFER);
  stream << this->name() << "(" << this->discretizer << ", " << (isChamfer ? "t = " : "r = ")
         << this->size;
  if (this->min_angle >= 0) stream << ", min_angle = " << this->min_angle;
  if (!this->convex) stream << ", convex = false";
  if (!this->concave) stream << ", concave = false";
  stream << ")";
  return stream.str();
}

// Without Manifold there is no blend builder, so the modules are left unregistered
// rather than registered and inert: an unregistered module raises an unknown-module
// error naming the line, where a module evaluating to no geometry would silently
// delete the model the operator was applied to.
#ifdef ENABLE_MANIFOLD
void register_builtin_fillet()
{
  Builtins::init("fillet", new BuiltinModule(builtin_fillet, &Feature::ExperimentalFillet),
                 {
                   "fillet(r = number)",
                   "fillet(r = number, min_angle = number, convex = bool, concave = bool)",
                 });
  Builtins::init("chamfer", new BuiltinModule(builtin_chamfer, &Feature::ExperimentalFillet),
                 {
                   "chamfer(t = number)",
                   "chamfer(t = number, min_angle = number, convex = bool, concave = bool)",
                 });
}
#else
void register_builtin_fillet() {}
#endif  // ENABLE_MANIFOLD
