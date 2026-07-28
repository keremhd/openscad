// Shape comparison for the fillet tools: does the solid the operator builds
// agree with a hand-written reference, everywhere, to within a tolerance?
//
// An exact symmetric difference is never empty — the operator's arcs and a
// hand-written cylinder never tessellate the same way — so each side is dilated
// by t before subtracting. The union of the two residuals is empty exactly when
// the Hausdorff distance between the solids is below t. Pick t above the chord
// error of the coarser tessellation and below the smallest defect worth
// catching, and tessellation slivers vanish while a wrong size, a missing corner
// or a gouge survives.
//
// The same comparison exists as a shell harness driven through the OpenSCAD
// language, where the dilation is a minkowski() and therefore runs on CGAL's Nef
// kernel — seconds per check. Here it is a few hundred convex hulls fed to
// Manifold, which is fast enough to sit in the unit-test run, and the failure
// arrives as a named case rather than a picture to interpret.

#include <catch2/catch_all.hpp>

#ifdef ENABLE_MANIFOLD

#include "geometry/fillet/FilletBuilder_internal.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include <manifold/manifold.h>

using namespace fillet::detail;
using manifold::Manifold;
using manifold::vec3;

namespace {

// Minkowski sum with a cube of half-extent t, i.e. dilation by t in L-infinity.
//
// The sum of a solid with a convex body is the solid itself unioned with the
// sum of each of its boundary triangles, and a triangle plus a cube is the hull
// of the 24 points you get by offsetting each of its corners to each corner of
// the cube. That makes this exact rather than an approximation, at the price of
// one hull per triangle — affordable on the small solids these cases use.
Manifold dilate(const Manifold& m, double t)
{
  if (m.IsEmpty()) return m;

  const manifold::MeshGL64 mesh = m.GetMeshGL64();
  const size_t stride = mesh.numProp;

  std::vector<Manifold> pieces{m};
  for (size_t i = 0; i + 2 < mesh.triVerts.size(); i += 3) {
    std::vector<vec3> pts;
    pts.reserve(24);
    for (size_t k = 0; k < 3; ++k) {
      const size_t base = mesh.triVerts[i + k] * stride;
      const double x = mesh.vertProperties[base];
      const double y = mesh.vertProperties[base + 1];
      const double z = mesh.vertProperties[base + 2];
      for (int corner = 0; corner < 8; ++corner)
        pts.emplace_back(x + ((corner & 1) ? t : -t), y + ((corner & 2) ? t : -t),
                         z + ((corner & 4) ? t : -t));
    }
    Manifold piece = Manifold::Hull(pts);
    if (!piece.IsEmpty()) pieces.push_back(std::move(piece));
  }
  return Manifold::BatchBoolean(pieces, manifold::OpType::Add);
}

// The two-sided containment residual: what of either solid lies further than t
// from the other. Empty means the two agree to within t everywhere.
Manifold residual(const Manifold& a, const Manifold& b, double t)
{
  return (a - dilate(b, t)) + (b - dilate(a, t));
}

// Does this solid enclose any material? Not the same question as IsEmpty():
// wherever a boolean's operands touch on a set of zero measure, Manifold leaves
// a volumeless four-triangle shell behind, and an exact containment is precisely
// such a case — so a residual that is geometrically nothing still has triangles
// in it. The cutoff is nine orders below the solids being compared: far under
// any defect a tolerance-based check could be about, far above float noise.
bool enclosesNothing(const Manifold& m, double scale)
{
  if (m.IsEmpty()) return true;
  const double keepAbove = 1e-9 * scale;
  for (const auto& part : m.Decompose())
    if (std::abs(part.Volume()) > keepAbove) return false;
  return true;
}

// Two solids agree within t. Both being empty counts as agreement; one empty and
// one not does not, which is what makes this catch an operator that silently
// builds nothing.
bool agreesWithin(const Manifold& a, const Manifold& b, double t)
{
  const double scale = std::max(std::abs(a.Volume()), std::abs(b.Volume()));
  return enclosesNothing(residual(a, b, t), scale);
}

// How finely the hand-written references tessellate their arcs. Manifold's own
// default is 12 segments a circle, whose chord error is larger than the 2 %
// tolerance these cases compare at — a reference coarser than the tolerance
// fails a correct tool, so it is set here rather than inherited.
constexpr int kRefSegments = 64;

// The chamfer/bevel wedge along one axis-aligned edge running in z: a triangular
// prism, and convex, so the hull of its six corners is the whole of it.
Manifold refWedgeZ(double cx, double cy, double t, double h, double z0, bool concave)
{
  const double s = concave ? 1.0 : -1.0;
  std::vector<vec3> pts;
  for (double z : {z0, z0 + h}) {
    pts.emplace_back(cx, cy, z);
    pts.emplace_back(cx + s * t, cy, z);
    pts.emplace_back(cx, cy + s * t, z);
  }
  return Manifold::Hull(pts);
}

// The fillet bead along one axis-aligned edge running in z: the corner square
// minus a quarter cylinder, the idiom a user would write by hand. `concave`
// picks which side of the corner the material is on, and with it whether the
// arc is subtracted from the reentrant square or from the sharp sliver.
Manifold refBeadZ(double cx, double cy, double r, double h, double z0, bool concave)
{
  const double s = concave ? 1.0 : -1.0;
  const double bx = concave ? cx : cx - r;
  const double by = concave ? cy : cy - r;
  const Manifold square = Manifold::Cube(vec3(r, r, h), false).Translate(vec3(bx, by, z0));
  const Manifold arc = Manifold::Cylinder(h + 2, r, r, kRefSegments, false)
                         .Translate(vec3(cx + s * r, cy + s * r, z0 - 1));
  return square - arc;
}

// A torus of tube radius r whose tube center circle sits at radius rho in the
// z = zc plane — the surface the rolling ball sweeps around a closed ring edge.
Manifold refTorus(double rho, double zc, double r)
{
  manifold::SimplePolygon circle;
  for (int i = 0; i < kRefSegments; ++i) {
    const double a = 2.0 * M_PI * i / kRefSegments;
    circle.emplace_back(rho + r * std::cos(a), r * std::sin(a));
  }
  return Manifold::Revolve({circle}, kRefSegments).Translate(vec3(0, 0, zc));
}

// The two closed-ring references, both the annulus-prism-minus-torus idiom of
// the hole-mouth sanity case: an annulus one radius tall spanning the ring, with
// the swept ball taken out of it.
Manifold refRoundHoleMouth(double rh, double zf, double r)
{
  const Manifold annulus =
    Manifold::Cylinder(r, rh + r, rh + r, kRefSegments, false) -
    Manifold::Cylinder(r + 2, rh, rh, kRefSegments, false).Translate(vec3(0, 0, -1));
  return (annulus - refTorus(rh + r, 0.0, r)).Translate(vec3(0, 0, zf - r));
}

Manifold refFilletBossBase(double rb, double zf, double r)
{
  const Manifold annulus =
    Manifold::Cylinder(r, rb + r, rb + r, kRefSegments, false) -
    Manifold::Cylinder(r + 2, rb, rb, kRefSegments, false).Translate(vec3(0, 0, -1));
  return (annulus - refTorus(rb + r, r, r)).Translate(vec3(0, 0, zf));
}

// The tool solid the operator builds for a target, without going through the
// node or the geometry evaluator.
Manifold toolFor(const Manifold& target, double t, bool concave, double thresholdDeg = 45.0)
{
  const MergedMesh mm = mergeMesh(target.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, thresholdDeg, concave));
  return buildWedgeSolid(mm, adj, chains, t, concave);
}

Manifold roundToolFor(const Manifold& target, double r, bool concave, int arcSegments,
                      double thresholdDeg = 45.0)
{
  const MergedMesh mm = mergeMesh(target.GetMeshGL64());
  const auto adj = buildEdgeAdjacency(mm.tris);
  const auto chains = buildChains(mm, selectedEdges(mm, adj, thresholdDeg, concave));
  return buildRoundSolid(mm, adj, chains, r, concave, arcSegments);
}

Manifold box(double sx, double sy, double sz) { return Manifold::Cube(vec3(sx, sy, sz), false); }

// Rounding a convex solid by r, exactly: shrink it by r and grow it back with a
// ball of r. Both halves are exact rather than idiomatic — eroding a polytope is
// pushing each of its face planes in by r, and growing a convex body by a ball
// is the hull of balls at its vertices — so this is the answer every edge and
// every corner of a convex model has to reproduce at once, corners included.
// The face planes and the vertices are read off the solid's own mesh, so a case
// states its model once instead of restating its geometry as a reference.
Manifold refRoundedConvex(const Manifold& solid, double r)
{
  const manifold::MeshGL64 mesh = solid.GetMeshGL64();
  const size_t stride = mesh.numProp;
  auto vert = [&](size_t i) {
    const size_t base = mesh.triVerts[i] * stride;
    return vec3(mesh.vertProperties[base], mesh.vertProperties[base + 1],
                mesh.vertProperties[base + 2]);
  };

  Manifold eroded = solid;
  std::vector<std::pair<vec3, double>> planes;
  for (size_t i = 0; i + 2 < mesh.triVerts.size(); i += 3) {
    const vec3 a = vert(i), b = vert(i + 1), c = vert(i + 2);
    const vec3 n = manifold::la::normalize(manifold::la::cross(b - a, c - a));
    const double d = manifold::la::dot(n, a);
    bool seen = false;
    for (const auto& [pn, pd] : planes)
      if (manifold::la::dot(pn, n) > 1.0 - 1e-9 && std::abs(pd - d) < 1e-9) { seen = true; break; }
    if (seen) continue;
    planes.emplace_back(n, d);
    eroded = eroded.TrimByPlane(-n, -(d - r));
  }

  std::vector<Manifold> balls;
  const manifold::MeshGL64 core = eroded.GetMeshGL64();
  for (size_t i = 0; i + 2 < core.vertProperties.size(); i += core.numProp)
    balls.push_back(Manifold::Sphere(r, kRefSegments)
                      .Translate(vec3(core.vertProperties[i], core.vertProperties[i + 1],
                                      core.vertProperties[i + 2])));
  return Manifold::Hull(balls);
}

}  // namespace

TEST_CASE("comparison: tessellation differences pass, size differences do not")
{
  // Calibration of the instrument itself, before any of it is pointed at the
  // operator. Two tessellations of one cylinder differ by the chord error of the
  // coarser one, r(1 - cos(pi/n)) = 0.048 at r=10, n=32: inside a tolerance of
  // 0.1, outside one of 0.01. A cylinder 10% wider is outside both.
  const double r = 10.0, h = 5.0;
  const auto coarse = Manifold::Cylinder(h, r, r, 32, false);
  const auto fine = Manifold::Cylinder(h, r, r, 64, false);
  const auto wide = Manifold::Cylinder(h, 1.1 * r, 1.1 * r, 64, false);

  CHECK(agreesWithin(coarse, fine, 0.1));
  CHECK_FALSE(agreesWithin(coarse, fine, 0.01));
  CHECK_FALSE(agreesWithin(coarse, wide, 0.1));

  // An empty candidate is not a match for a solid reference — the case that
  // separates "the operator agrees" from "the operator did nothing".
  CHECK_FALSE(agreesWithin(Manifold(), fine, 0.1));
}

TEST_CASE("chamfer_tool: the inner corner wedge matches the hand-written reference")
{
  // The L of two overlapping bars has exactly one concave crease, running the
  // full height at (x,y) = (6,6), so the whole tool is one wedge and no clipping
  // is needed to compare against one.
  const double t = 3.0, h = 10.0;
  const auto model = box(20.0, 6.0, h) + box(6.0, 20.0, h);

  const auto tool = toolFor(model, t, /*concave=*/true);
  REQUIRE_FALSE(tool.IsEmpty());

  const auto ref = refWedgeZ(6.0, 6.0, t, h, 0.0, /*concave=*/true);
  CHECK(agreesWithin(tool, ref, 0.02 * t));

  // And the point of the tool: unioning it fills the crease, matching the model
  // with the reference wedge already in it.
  CHECK(agreesWithin(model + tool, model + ref, 0.02 * t));
}

TEST_CASE("bevel_tool: one cube edge matches the hand-written reference")
{
  // Every solid has plenty of convex edges and the tool takes all of them, so
  // the comparison is clipped to a box around the one edge the reference covers
  // — clear of the corners, where the two constructions genuinely differ until
  // junction cells exist.
  const double s = 16.0, t = 2.0;
  const auto model = box(s, s, s);

  const auto tool = toolFor(model, t, /*concave=*/false);
  REQUIRE_FALSE(tool.IsEmpty());

  const double margin = 2.0 * t;
  const auto clip =
    Manifold::Cube(vec3(2 * margin, 2 * margin, s - 2 * margin), false)
      .Translate(vec3(s - margin, s - margin, margin));

  const auto ref = refWedgeZ(s, s, t, s, 0.0, /*concave=*/false);
  CHECK(agreesWithin(tool ^ clip, ref ^ clip, 0.02 * t));

  // Applied, i.e. subtracted, which is the sign the caller uses.
  CHECK(agreesWithin((model - tool) ^ clip, (model - ref) ^ clip, 0.02 * t));
}

TEST_CASE("fillet_tool: the inner corner bead matches the hand-written reference")
{
  // Same L as the chamfer case, and the same single crease, but now the tool has
  // to be the corner square with a quarter cylinder taken out of it rather than
  // the square's diagonal.
  const double r = 3.0, h = 10.0;
  const auto model = box(20.0, 6.0, h) + box(6.0, 20.0, h);

  const auto tool = roundToolFor(model, r, /*concave=*/true, 48);
  REQUIRE_FALSE(tool.IsEmpty());

  const auto ref = refBeadZ(6.0, 6.0, r, h, 0.0, /*concave=*/true);
  CHECK(agreesWithin(tool, ref, 0.02 * r));
  CHECK(agreesWithin(model + tool, model + ref, 0.02 * r));

  // The bead is strictly smaller than the chamfer that shares its tangency
  // points, by exactly the circular segment. Without this a tool that quietly
  // skipped the subtraction would still match a chamfer-shaped reference.
  CHECK_FALSE(agreesWithin(tool, toolFor(model, r, /*concave=*/true), 0.02 * r));
}

TEST_CASE("round_tool: one cube edge matches the hand-written reference")
{
  // Clipped to the one edge the reference covers, clear of the corners, for the
  // same reason the bevel case is: the tool takes all twelve.
  const double s = 16.0, r = 2.0;
  const auto model = box(s, s, s);

  const auto tool = roundToolFor(model, r, /*concave=*/false, 48);
  REQUIRE_FALSE(tool.IsEmpty());

  const double margin = 2.0 * r;
  const auto clip = Manifold::Cube(vec3(2 * margin, 2 * margin, s - 2 * margin), false)
                      .Translate(vec3(s - margin, s - margin, margin));

  const auto ref = refBeadZ(s, s, r, s, 0.0, /*concave=*/false);
  CHECK(agreesWithin(tool ^ clip, ref ^ clip, 0.02 * r));
  CHECK(agreesWithin((model - tool) ^ clip, (model - ref) ^ clip, 0.02 * r));
}

TEST_CASE("round_tool: beads meeting at a corner do not hollow each other out")
{
  // Three creases meet at every cube corner and each rolls its own ball. Those
  // balls reach past the corner into the neighbouring beads, and subtracting all
  // of them from all the wedges at once would eat away the very material the
  // neighbours are there to remove — unless each spine stops where its ball
  // first touches the third wall, which is what makes the canal exactly the set
  // of positions the ball can reach.
  //
  // The probe sits in the top edge's bead, outside that bead's own ball, and
  // 3.35 from the corner ball's centre against a radius of 3 — so it is tool on
  // every count, and it is what an untruncated spine would wrongly remove.
  const double s = 16.0, r = 3.0;
  const auto model = box(s, s, s);

  const auto tool = roundToolFor(model, r, /*concave=*/false, 48);
  REQUIRE_FALSE(tool.IsEmpty());

  const auto probe = box(0.4, 0.4, 0.4).Translate(vec3(13.8, 15.3, 15.3));
  CHECK(enclosesNothing(probe - tool, probe.Volume()));
}

TEST_CASE("round_tool: the cube corner closes onto the true rounded solid")
{
  // The acceptance the edge cells alone cannot reach: three beads meeting at a
  // corner leave a lump there whatever the radius, and only a corner cell with
  // its own ball taken out of it gives the spherical patch. Compared against the
  // exact shrink-and-grow answer, which pins the twelve edges and the eight
  // corners in the same statement.
  const double s = 16.0, r = 3.0;
  const auto model = box(s, s, s);

  const auto tool = roundToolFor(model, r, /*concave=*/false, 48);
  REQUIRE_FALSE(tool.IsEmpty());

  CHECK(agreesWithin(model - tool, refRoundedConvex(model, r), 0.02 * r));
}

TEST_CASE("round_tool: a three-face and a four-face apex both close")
{
  // Corners of valence three and four in one statement. A low-$fn cone is a
  // pyramid, so the apex is where three (or four) slant creases meet at once and
  // every base corner is a valence-three junction of two base creases and a
  // slant — and being convex, the whole rounded solid has the exact
  // shrink-and-grow answer, apex included.
  //
  // Four walls do not generally leave the ball one place to sit, which is why
  // the corner is solved as every triple of walls filtered down to the positions
  // that clear the rest. A symmetric apex is the case where those all coincide;
  // it still has to come out right, and it is the one that says the filter has
  // not thrown the answer away.
  const double R = 30.0, h = 60.0, r = 6.0;

  for (const int sides : {3, 4}) {
    const auto model = Manifold::Cylinder(h, R, 0.0, sides, false);
    const auto tool = roundToolFor(model, r, /*concave=*/false, 24);
    REQUIRE_FALSE(tool.IsEmpty());
    const auto ref = refRoundedConvex(model, r);
    REQUIRE_FALSE(ref.IsEmpty());
    CHECK(agreesWithin(model - tool, ref, 0.02 * r));
  }
}

TEST_CASE("fillet_tool: a three-face and a four-face pocket both close")
{
  // The same two apexes turned inside out: a pyramidal pocket in a block, where
  // the creases are concave and the ball rolls in the void rather than the
  // solid. The void is the pyramid, so filling the pocket's creases is rounding
  // that pyramid's own edges — the same exact answer, subtracted from the block
  // instead of from the model.
  const double R = 30.0, h = 60.0, r = 6.0, cap = 5.0;

  for (const int sides : {3, 4}) {
    const auto pocket = Manifold::Cylinder(h, R, 0.0, sides, false);
    // The block runs past the tip, so the pocket ends inside it. Flush, the
    // solid would pinch to a point at the apex, which is a property of the model
    // and not of the fillet, but it makes every downstream boolean harder.
    const auto block = box(4 * R, 4 * R, h + cap).Translate(vec3(-2 * R, -2 * R, 0));
    const auto model = block - pocket;

    const auto tool = roundToolFor(model, r, /*concave=*/true, 24);
    REQUIRE_FALSE(tool.IsEmpty());
    const auto ref = block - refRoundedConvex(pocket, r);
    REQUIRE_FALSE(ref.IsEmpty());

    // Clipped above the pocket's mouth. The rim where it opens is a convex edge
    // of the block, not a concave one — the pocket walls lean away from the
    // bottom face rather than into it — so `fillet_tool` rightly leaves it alone
    // while rounding the void rounds it too. Everything the case is about, the
    // slant creases and the apex they meet at, is well clear of it.
    const auto clip = box(4 * R, 4 * R, h + cap).Translate(vec3(-2 * R, -2 * R, 2 * r));
    CHECK(agreesWithin((model + tool) ^ clip, ref ^ clip, 0.02 * r));
  }
}

TEST_CASE("fillet_tool: the inside box corner closes")
{
  // The concave counterpart, and the case §6.3 is written about: an octant
  // notched out of a block leaves three concave creases meeting at one reentrant
  // corner. Filling it correctly means the *void* ends up rounded, and the void
  // is convex where it matters, so the same hull-of-balls reference applies to
  // it — placed so that only the notch's own corner is inside the block and the
  // far ones fall outside, where the block clips them away.
  const double s = 40.0, cut = 20.0, r = 4.0;
  const auto block = box(s, s, s);
  const auto model = block - box(cut, cut, cut).Translate(vec3(cut, cut, cut));

  const auto tool = roundToolFor(model, r, /*concave=*/true, 48);
  REQUIRE_FALSE(tool.IsEmpty());

  const double far = 3.0 * s;
  std::vector<Manifold> balls;
  for (int c = 0; c < 8; ++c)
    balls.push_back(Manifold::Sphere(r, kRefSegments)
                      .Translate(vec3((c & 1) ? far : cut + r, (c & 2) ? far : cut + r,
                                      (c & 4) ? far : cut + r)));
  const auto ref = block - Manifold::Hull(balls);

  CHECK(agreesWithin(model + tool, ref, 0.02 * r));

  // And the statement that pins the corner numerically rather than by its
  // bounds. A tenth of a radius above the corner, the ball at (cut+r)^3 cuts the
  // plane in a circle of radius sqrt(r^2 - (0.9r)^2), so the remaining void is
  // the notch square pulled back to that circle and rounded on it — the hull of
  // four such circles, three of them pushed out of the block. Get the corner
  // wrong in either direction and this misses, at a tolerance a fiftieth of r.
  const double z = cut + 0.1 * r;
  const double rq = std::sqrt(r * r - 0.81 * r * r);
  const auto slab = box(s, s, 0.002).Translate(vec3(0, 0, z - 0.001));

  std::vector<Manifold> pillars;
  for (int c = 0; c < 4; ++c)
    pillars.push_back(Manifold::Cylinder(s, rq, rq, kRefSegments, false)
                        .Translate(vec3((c & 1) ? far : cut + r, (c & 2) ? far : cut + r, 0)));

  CHECK(agreesWithin((block - (model + tool)) ^ slab, Manifold::Hull(pillars) ^ slab, 0.02 * r));
}

TEST_CASE("round_tool: the hole mouth matches annulus prism minus torus")
{
  // The sanity case the whole operator was specified against: rounding the mouth
  // of a through hole has to give the shape a user would write by hand as an
  // annulus prism with a torus taken out of it. A closed convex ring, so it also
  // covers the chain wrapping.
  const double w = 60.0, t = 20.0, rh = 10.0, r = 3.0;
  const auto model = box(w, w, t).Translate(vec3(-w / 2, -w / 2, 0)) -
                     Manifold::Cylinder(t + 2, rh, rh, 64, false).Translate(vec3(0, 0, -1));

  const auto tool = roundToolFor(model, r, /*concave=*/false, 48);
  REQUIRE_FALSE(tool.IsEmpty());

  // The operator rounds the far rim and the plate's own outer edges too; the
  // reference covers the top mouth, so the comparison is clipped to it.
  const double c = 2.0 * r;
  const auto clip = Manifold::Cylinder(c + 1, rh + c, rh + c, 64, false).Translate(vec3(0, 0, t - c));

  const auto ref = refRoundHoleMouth(rh, t, r);
  CHECK(agreesWithin(tool ^ clip, ref ^ clip, 0.02 * r));
  CHECK(agreesWithin((model - tool) ^ clip, (model - ref) ^ clip, 0.02 * r));
}

TEST_CASE("fillet_tool: a boss base fillet larger than the boss still matches")
{
  // The concave closed ring, at a radius twice the boss's own: the section plane
  // has to be the meridian one and the ring has to stay a ring, or the bead
  // wanders off the axis. The section plane is taken from the two wall normals
  // rather than from the spine direction precisely so this holds.
  const double rb = 12.0, r = 24.0;
  const auto model = box(80.0, 80.0, 15.0).Translate(vec3(-40, -40, -15)) +
                     Manifold::Cylinder(40.0, rb, rb, 48, false);

  const auto tool = roundToolFor(model, r, /*concave=*/true, 48);
  REQUIRE_FALSE(tool.IsEmpty());

  const auto ref = refFilletBossBase(rb, 0.0, r);
  CHECK(agreesWithin(tool, ref, 0.02 * r));
}

TEST_CASE("comparison: a wedge of the wrong size is caught")
{
  // The self-test the shell harness carries too: the comparison has to fail when
  // it should, or a green run means nothing. A chamfer 20% too deep is well
  // outside the 2% tolerance the cases above pass at.
  const double t = 3.0, h = 10.0;
  const auto model = box(20.0, 6.0, h) + box(6.0, 20.0, h);

  const auto tool = toolFor(model, t, /*concave=*/true);
  const auto refTooBig = refWedgeZ(6.0, 6.0, 1.2 * t, h, 0.0, /*concave=*/true);
  CHECK_FALSE(agreesWithin(tool, refTooBig, 0.02 * t));
}


TEST_CASE("round_tool: asymmetric junctions of valence four, five and six")
{
  // The case the corner solve is actually general for. Shearing a pyramid moves
  // its apex off the axis, so the incident walls pushed in by r no longer share
  // one point and the ball has several extreme positions there rather than one —
  // four sides give two, five give three, six give four. Each is tangent to
  // three walls and clear of the rest; a triple that is tangent to its own three
  // and buried in a fourth would gouge the fillet back from that fourth wall,
  // and discarding those is what the filter is for.
  //
  // Radius kept well inside the feature. Larger, and neighbouring beads on the
  // shallowest creases (this shape has one at 130 degrees, where the tangency
  // setback is over twice r) start colliding, which is the unsettled oversize
  // question and not a statement about the corner.
  const double R = 30.0, h = 60.0, r = 2.0;

  for (const int sides : {4, 5, 6}) {
    CAPTURE(sides);
    manifold::mat3x4 shear = manifold::la::identity;
    shear[2][0] = 0.45;  // x += 0.45 z
    shear[2][1] = 0.20;
    const auto model = Manifold::Cylinder(h, R, 0.0, sides, false).Transform(shear);

    // The apex really is the multi-centre case, not a symmetric one in disguise.
    const auto mm = mergeMesh(model.GetMeshGL64());
    const auto adj = buildEdgeAdjacency(mm.tris);
    const auto chains = buildChains(mm, selectedEdges(mm, adj, 45.0, /*concave=*/false));
    size_t apexCentres = 0;
    for (const auto& j : chainJunctions(mm, adj, chains, r, /*concave=*/false))
      if (j.faceNormals.size() == static_cast<size_t>(sides)) apexCentres = j.ballCentres.size();
    CHECK(apexCentres == static_cast<size_t>(sides - 2));

    const auto tool = roundToolFor(model, r, /*concave=*/false, 24);
    REQUIRE_FALSE(tool.IsEmpty());
    CHECK(agreesWithin(model - tool, refRoundedConvex(model, r), 0.02 * r));

    // And the same solid as a pocket, so the concave sign gets the same corners.
    // Clipped above the mouth, whose rim is a convex edge the tool leaves alone.
    const auto block = box(6 * R, 6 * R, h + 5.0).Translate(vec3(-3 * R, -3 * R, 0));
    const auto pocket = block - model;
    const auto pocketTool = roundToolFor(pocket, r, /*concave=*/true, 24);
    REQUIRE_FALSE(pocketTool.IsEmpty());
    const auto clip = box(6 * R, 6 * R, h + 5.0).Translate(vec3(-3 * R, -3 * R, 2 * r));
    CHECK(agreesWithin((pocket + pocketTool) ^ clip,
                       (block - refRoundedConvex(model, r)) ^ clip, 0.02 * r));
  }
}

#endif  // ENABLE_MANIFOLD
