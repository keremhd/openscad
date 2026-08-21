// A concave blend of radius R built by hand, with a convex edge D above it.
//
// No fillet module builds the blend. The 2D profile is a polygon whose blend
// edges lie on the tangent lines of the circle of radius R centred at (R,R):
// the first tangent line IS the wall (x=0) and the last IS the floor (y=0), so
// the blend meets both walls tangentially by construction, and every crease
// along it -- the two boundary ones included -- turns by exactly DELTA.
// Choosing K = $fn/4 makes DELTA = 360/$fn, the same facet angle a circle of
// this $fn would have. So the blend boundary is not a crease: it is exactly as
// smooth as the blend's interior, and the classifier -- whose default threshold
// is a constant 46 degrees -- cannot see it at all. A right-angle corner cannot
// produce one that it can: the coarsest tangent blend of 90 degrees is a single
// 45-degree chamfer, and 45 < 46. Set MINANG below DELTA to make the boundary
// register as a crease; that is the one knob that changes the verdict.
//
// This file is a measurement model, not a bench tile: it sets $fn, which the
// bench's own models never do, and it is deliberately absent from expect.txt.
//
//   walls:  the block face x=0 (a wall of the step) and the floor y=0
//   blend:  tangent to x=0 at (0,R) and to y=0 at (R,0)
//   convex test edge: (0,H) with H = R + D, so D is the length of flat wall
//                     between the blend's tangency line and the convex edge
//   convex control edge: (-W,H), whose face runs flat for H+T -- abundant room
//
// MODE = "round"  difference(part, round_tool(r=RT) part)   the measurement
// MODE = "plain"  the hand-built solid alone                the profile check
// MODE = "tool"   the round_tool solid alone

FNSET = 48;
$fn = FNSET; $fa = 360/FNSET; $fs = 0.01;

R  = 1.0;        // blend radius, built by hand
RT = 1.0;        // round_tool radius (kept equal to R: that is the entanglement)
D  = 1.0;        // flat wall between blend tangency and the convex edge
W  = 8;          // block width left of the wall
F  = 12;         // floor length right of the wall
T  = 4;          // floor thickness
L  = 20;         // extrusion length
KSET  = 0;      // blend facets over the quarter turn; 0 = FNSET/4, so DELTA = 360/$fn
MODE  = "round";
BRUSH = false;   // true: hand round_tool a brush covering only the test edge,
                 // so accept/refuse is about that crease and nothing else
MINANG = -1;     // round_tool min_angle=; -1 keeps the default 46 deg constant
DBG   = false;   // true: swap the tool for markers, purely to get the count echo

H     = R + D;
K     = KSET > 0 ? KSET : floor(FNSET/4);
DELTA = 90/K;
RC    = R/cos(DELTA/2);   // circumscribed vertex radius: edges touch at radius R

function bv(i) = [R,R] + RC*[cos(180+(i+0.5)*DELTA), sin(180+(i+0.5)*DELTA)];

prof = concat([[-W,-T],[F,-T],[F,0]],
              [for (i=[K-1:-1:0]) bv(i)],
              [[0,H],[-W,H]]);

module part() linear_extrude(height = L) polygon(prof);
// A box around the middle of the test edge only. Well clear of the two corner
// junctions at z=0 and z=L, which the gate exempts out to 2*RT anyway.
module testbrush() translate([0, H, L/2]) cube([RT, RT, L/2], center = true);

// convex = the round-off half (old round_tool): rounds the convex test edge, and
// leaves the hand-built concave blend alone.
module rounded() {
    if (BRUSH) fillet(r = RT, min_angle = MINANG, concave = false) { part(); testbrush(); }
    else fillet(r = RT, min_angle = MINANG, concave = false) part();
}

if (MODE == "plain") part();
else rounded();
