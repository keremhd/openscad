#pragma once

#include <memory>

class Geometry;
class ManifoldGeometry;
class FilletNode;

// The fillet operator's geometry core: extract the target mesh, rebuild
// edge -> two-face adjacency from the triangle soup, and classify each edge
// (concave/convex plus a tessellation-seam filter) as the basis for building
// the tool solid.
//
// Returns the finished tool solid, or nullptr while the operator is still a
// classify-only no-op (currently it only logs diagnostic edge counts).
std::shared_ptr<const Geometry> buildFilletTool(
  const FilletNode& node, const std::shared_ptr<const ManifoldGeometry>& target);
