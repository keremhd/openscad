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

#include "geometry/fillet/FilletBrush.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <manifold/manifold.h>

#include "geometry/linalg.h"

namespace fillet::detail {

namespace {

// Below this many faces a node stops splitting; the traversal overhead past that
// costs more than testing the faces outright.
constexpr int kLeafSize = 4;

// How far outside a triangle, in barycentric terms, still counts as hitting it.
// A ray that meets a face exactly on an edge or a vertex has to be caught by
// every triangle that owns it, not by none of them: a brush is drawn symmetric
// about the crease it selects far more often than not — a column straddling one
// edge of a cube is the standard way to name that edge — and the spine then runs
// exactly through the diagonal where the two triangles of the brush's end face
// meet. Tested exactly, both reject on opposite sides of a coordinate that is 1
// to within an ulp, the crossing is lost, and the brush silently stops bounding
// the crease along its length. Caught by both, the pair is one passage in the
// same direction and collapses to a single crossing below. A ninth of a
// nanometre of barycentric slack is seven orders above that noise and far below
// any brush face a model could mean to place.
constexpr double kEdgeSlack = 1e-9;

// Moller-Trumbore, with `d` left unnormalized so `t` comes back in the caller's
// own parameter (a segment's 0..1, or a distance when d is a unit vector).
// Returns false for a ray parallel to the triangle's plane, which is the one
// case with no well-defined crossing.
bool rayFace(const Vector3d& o, const Vector3d& d, const Vector3d& a, const Vector3d& ab,
             const Vector3d& ac, double& t)
{
  const Vector3d p = d.cross(ac);
  const double det = ab.dot(p);
  if (std::abs(det) < 1e-14) return false;
  const double inv = 1.0 / det;

  const Vector3d tv = o - a;
  const double u = tv.dot(p) * inv;
  if (u < -kEdgeSlack || u > 1.0 + kEdgeSlack) return false;

  const Vector3d q = tv.cross(ab);
  const double v = d.dot(q) * inv;
  if (v < -kEdgeSlack || u + v > 1.0 + kEdgeSlack) return false;

  t = ac.dot(q) * inv;
  return true;
}

// Slab test against an axis-aligned box, for the ray o + t*d over [0, tmax].
bool rayBox(const Vector3d& o, const Vector3d& invD, double tmax, const Vector3d& lo,
            const Vector3d& hi)
{
  double t0 = 0.0, t1 = tmax;
  for (int k = 0; k < 3; ++k) {
    double a = (lo[k] - o[k]) * invD[k];
    double b = (hi[k] - o[k]) * invD[k];
    if (a > b) std::swap(a, b);
    t0 = std::max(t0, a);
    t1 = std::min(t1, b);
    if (t0 > t1) return false;
  }
  return true;
}

}  // namespace

BrushVolume::BrushVolume(const manifold::MeshGL64& mesh)
{
  const size_t numProp = mesh.numProp;
  if (numProp < 3) return;
  const size_t numTri = mesh.triVerts.size() / 3;

  auto vertexAt = [&](size_t i) {
    return Vector3d(mesh.vertProperties[i * numProp + 0], mesh.vertProperties[i * numProp + 1],
                    mesh.vertProperties[i * numProp + 2]);
  };

  faces_.reserve(numTri);
  std::vector<Vector3d> centroid;
  centroid.reserve(numTri);
  for (size_t t = 0; t < numTri; ++t) {
    const Vector3d a = vertexAt(mesh.triVerts[t * 3 + 0]);
    const Vector3d b = vertexAt(mesh.triVerts[t * 3 + 1]);
    const Vector3d c = vertexAt(mesh.triVerts[t * 3 + 2]);
    Vector3d n = (b - a).cross(c - a);
    const double len = n.norm();
    // A zero-area triangle has no side to be on, so it can neither be entered
    // nor left; dropping it here keeps every crossing signed.
    if (!(len > 0)) continue;
    faces_.push_back({a, b - a, c - a, n / len});
    centroid.push_back((a + b + c) / 3.0);
  }

  if (faces_.empty()) return;
  order_.resize(faces_.size());
  for (size_t i = 0; i < order_.size(); ++i) order_[i] = static_cast<int>(i);
  nodes_.reserve(2 * faces_.size());
  buildNode(0, static_cast<int>(order_.size()), centroid);
}

int BrushVolume::buildNode(int start, int count, const std::vector<Vector3d>& centroid)
{
  const int self = static_cast<int>(nodes_.size());
  nodes_.emplace_back();

  Vector3d lo = Vector3d::Constant(std::numeric_limits<double>::infinity());
  Vector3d hi = -lo;
  for (int i = start; i < start + count; ++i) {
    const Face& f = faces_[order_[i]];
    for (const Vector3d p : {f.a, Vector3d(f.a + f.ab), Vector3d(f.a + f.ac)}) {
      lo = lo.cwiseMin(p);
      hi = hi.cwiseMax(p);
    }
  }
  nodes_[self].lo = lo;
  nodes_[self].hi = hi;

  if (count <= kLeafSize) {
    nodes_[self].start = start;
    nodes_[self].count = count;
    return self;
  }

  // Split at the median along whichever axis the centroids spread widest, which
  // keeps the tree balanced without needing a cost model a selection brush -- a
  // cube, a cylinder -- would never pay off.
  Vector3d clo = Vector3d::Constant(std::numeric_limits<double>::infinity());
  Vector3d chi = -clo;
  for (int i = start; i < start + count; ++i) {
    clo = clo.cwiseMin(centroid[order_[i]]);
    chi = chi.cwiseMax(centroid[order_[i]]);
  }
  int axis = 0;
  const Vector3d spread = chi - clo;
  if (spread[1] > spread[axis]) axis = 1;
  if (spread[2] > spread[axis]) axis = 2;

  const int mid = start + count / 2;
  std::nth_element(order_.begin() + start, order_.begin() + mid, order_.begin() + start + count,
                   [&](int a, int b) { return centroid[a][axis] < centroid[b][axis]; });

  buildNode(start, mid - start, centroid);
  nodes_[self].right = buildNode(mid, start + count - mid, centroid);
  return self;
}

void BrushVolume::candidates(const Vector3d& o, const Vector3d& d, double tmax,
                             std::vector<int>& out) const
{
  out.clear();
  if (nodes_.empty()) return;

  // A zero component makes the slab bounds infinite, which the min/max handles
  // correctly as "this axis never rejects".
  Vector3d invD;
  for (int k = 0; k < 3; ++k)
    invD[k] = d[k] != 0.0 ? 1.0 / d[k] : std::numeric_limits<double>::infinity();

  std::vector<int> stack{0};
  while (!stack.empty()) {
    const int self = stack.back();
    stack.pop_back();
    const Node& node = nodes_[self];
    if (!rayBox(o, invD, tmax, node.lo, node.hi)) continue;
    if (node.count > 0) {
      for (int i = node.start; i < node.start + node.count; ++i) out.push_back(order_[i]);
      continue;
    }
    // Both children are visited whatever the order, since every candidate is
    // tested anyway; there is no nearest-hit early out to order them for.
    stack.push_back(node.right);
    stack.push_back(self + 1);
  }
}

std::vector<BrushVolume::Crossing> BrushVolume::segmentCrossings(const Vector3d& a,
                                                                 const Vector3d& b) const
{
  std::vector<Crossing> out;
  if (faces_.empty()) return out;

  const Vector3d d = b - a;
  const double length = d.norm();
  if (!(length > 0)) return out;

  std::vector<int> hits;
  candidates(a, d, 1.0, hits);
  for (const int i : hits) {
    const Face& f = faces_[i];
    double t = 0.0;
    if (!rayFace(a, d, f.a, f.ab, f.ac, t)) continue;
    if (t < 0.0 || t > 1.0) continue;
    out.push_back({t, d.dot(f.n) < 0.0});
  }

  std::sort(out.begin(), out.end(), [](const Crossing& x, const Crossing& y) {
    if (x.t != y.t) return x.t < y.t;
    return x.entering && !y.entering;
  });

  // A segment through a shared edge or a vertex meets both (or all) of the
  // triangles that own it, at the same parameter and with the same direction:
  // one passage, reported once per face. Collapse those. A pair with opposite
  // directions is a genuine graze -- in and straight back out -- and is left
  // alone for the caller's minimum-length guard to discard as an interval.
  const double tol = 1e-9 * std::max(1.0, length);
  std::vector<Crossing> merged;
  for (const Crossing& c : out)
    if (merged.empty() || c.entering != merged.back().entering ||
        c.t - merged.back().t > tol / length)
      merged.push_back(c);
  return merged;
}

bool BrushVolume::contains(const Vector3d& p) const
{
  if (faces_.empty()) return false;

  // Three directions chosen to share no plane with an axis-aligned brush, which
  // is what most selection brushes are. Each is a separate vote so that a ray
  // that leaves through an edge -- where the nearest face's orientation says
  // nothing -- is outvoted rather than believed.
  static const Vector3d kDirs[3] = {
    Vector3d(0.2673, 0.5345, 0.8018),
    Vector3d(-0.7071, 0.3162, 0.6325).normalized(),
    Vector3d(0.4082, -0.8165, 0.4082),
  };

  int votes = 0;
  std::vector<int> hits;
  for (const Vector3d& dir : kDirs) {
    candidates(p, dir, std::numeric_limits<double>::infinity(), hits);
    double best = std::numeric_limits<double>::infinity();
    double bestSide = 0.0;
    for (const int i : hits) {
      const Face& f = faces_[i];
      double t = 0.0;
      if (!rayFace(p, dir, f.a, f.ab, f.ac, t)) continue;
      if (t < 0.0 || t >= best) continue;
      const double side = dir.dot(f.n);
      // A face the ray only grazes cannot say which side it was on. Skipping it
      // is safe: whatever is behind it answers instead, and if nothing is, the
      // other two directions still vote.
      if (std::abs(side) < 1e-6) continue;
      best = t;
      bestSide = side;
    }
    // Leaving through a face whose outward normal points along the ray means the
    // ray started inside the material that face bounds.
    if (bestSide > 0.0) ++votes;
  }
  return votes >= 2;
}

}  // namespace fillet::detail
