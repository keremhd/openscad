#pragma once

#include <string>
#include <utility>

#include "core/CurveDiscretizer.h"
#include "core/ModuleInstantiation.h"
#include "core/node.h"
#include "geometry/linalg.h"

// The four fillet/round/chamfer/bevel tool nodes. Convexity sign is baked into
// the node name (see fillet-operator-plan.md §2): FILLET/CHAMFER are concave
// tools the caller unions; ROUND/BEVEL are convex tools the caller subtracts.
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
  // override for the auto-derived dihedral threshold; <0 means "auto" (§4.3)
  double min_angle{-1.0};
  FilletType type;
  CurveDiscretizer discretizer;
};
