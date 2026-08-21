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

#include <cstddef>
#include <vector>

#include <manifold/manifold.h>

#include "geometry/linalg.h"

namespace fillet::detail {

// The selection brushes, reduced to the two questions clipping a spine asks of
// them: where a spine segment crosses the boundary, and which side of it a point
// is on. Both are answered by shooting a ray at the brush triangles through an
// AABB tree, so the cost follows the spine rather than the brush.
//
// The brush is a solid, not a surface, so a crossing carries which way it goes.
// Taking that from the sign of the ray against the triangle's outward normal —
// rather than from counting crossings and alternating — means a chain needs no
// global parity test unless it crosses nothing at all, and one bad crossing
// cannot invert every interval after it.
class BrushVolume
{
public:
  explicit BrushVolume(const manifold::MeshGL64& mesh);

  bool empty() const { return faces_.empty(); }

  // A point where a segment passes through the brush's boundary: how far along
  // the segment, and whether the brush is being entered there.
  struct Crossing
  {
    double t;
    bool entering;
  };

  // The crossings along the segment a -> b, ordered, with the pairs a segment
  // grazing an edge or a vertex of the brush reports twice collapsed to one.
  std::vector<Crossing> segmentCrossings(const Vector3d& a, const Vector3d& b) const;

  // Whether p is inside the brush. Answered from the orientation of the nearest
  // triangle a ray from p meets, not from a crossing count: a ray that leaves
  // through a back face started inside. Three directions are tried and the
  // majority wins, so a ray that happens to graze an edge does not decide it.
  bool contains(const Vector3d& p) const;

private:
  // One triangle, stored as the edge vectors Moller-Trumbore wants plus the
  // outward normal that gives a crossing its direction.
  struct Face
  {
    Vector3d a, ab, ac, n;
  };

  // An AABB tree node. `count` is zero for an interior node, whose children are
  // the next node and `right`; a leaf owns faces_[start, start + count).
  struct Node
  {
    Vector3d lo, hi;
    int start = 0;
    int count = 0;
    int right = -1;
  };

  // The faces whose boxes the ray o + t*d, t in [0, tmax], could meet.
  void candidates(const Vector3d& o, const Vector3d& d, double tmax, std::vector<int>& out) const;

  // Median-split over the centroids of order_[start, start + count).
  int buildNode(int start, int count, const std::vector<Vector3d>& centroid);

  std::vector<Face> faces_;
  std::vector<int> order_;  // face indices, permuted so each leaf is contiguous
  std::vector<Node> nodes_;
};

}  // namespace fillet::detail
