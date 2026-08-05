// Cutaway rig for cross.scad. Not a tracked file; lives in work/.
// R    fillet radius
// MODE 0 whole, 1 remove z>C, 2 remove octant x,y,z>0, 3 slab about plane x=y,
//      4 remove half-space along the body diagonal (x+y+z > C)
FNSET = 0; $fn = FNSET;
R = 2.0;
MODE = 0;
C = 0;
T = 2;   // slab thickness for MODE 3

module solid() {
    fillet(r = R) union() {
        cylinder(d = 12, h = 40, center = true);
        rotate([0, 90, 0]) cylinder(d = 12, h = 40, center = true);
        rotate([90, 0, 0]) cylinder(d = 12, h = 40, center = true);
    }
}

if (MODE == 0) solid();
else if (MODE == 1) difference() { solid(); translate([-50, -50, C]) cube([100, 100, 100]); }
else if (MODE == 2) difference() { solid(); translate([C, C, C]) cube([100, 100, 100]); }
else if (MODE == 3) intersection() { solid(); rotate([0, 0, 45]) translate([-T/2 + C, -50, -50]) cube([T, 100, 100]); }
else if (MODE == 4) difference() { solid(); rotate([0, 0, 45]) rotate([0, 54.7356, 0]) translate([C, -50, -50]) cube([100, 100, 100]); }

// MODE 5: remove the octant sx*x>C, sy*y>C, sz*z>C  (probe: which corner holds a handle)
SX = 1; SY = 1; SZ = 1; L = 10;
if (MODE == 5) difference() { solid(); scale([SX, SY, SZ]) translate([C, C, C]) cube([100, 100, 100]); }
// MODE 6: keep only |x|,|y|,|z| < L  (probe: what radius shell holds the handles)
if (MODE == 6) intersection() { solid(); cube([2*L, 2*L, 2*L], center = true); }

// MODE 7: keep only what lies inside a probe sphere at P, radius RHO
PX = 0; PY = 0; PZ = 0; RHO = 3.5;
if (MODE == 7) intersection() { solid(); translate([PX, PY, PZ]) sphere(r = RHO, $fn = 48); }

// MODE 8: remove the half-space { p . unit(D) > C }  (a plane cut in any direction)
DX = 1; DY = 1; DZ = -1;
module halfspace(D, c) {
    u = D / norm(D);
    ax = cross([0, 0, 1], u);
    an = acos(u[2]);
    rotate(an, norm(ax) < 1e-9 ? [1, 0, 0] : ax) translate([0, 0, c + 100]) cube(200, center = true);
}
if (MODE == 8) difference() { solid(); halfspace([DX, DY, DZ], C); }

// MODE 9: thin slab normal to axis AX at coordinate C, thickness T.
// For a thin plate the reported genus counts the internal holes of the 2D section.
AX = 2;
if (MODE == 9) intersection() {
    solid();
    if (AX == 0) translate([C, 0, 0]) cube([T, 100, 100], center = true);
    else if (AX == 1) translate([0, C, 0]) cube([100, T, 100], center = true);
    else translate([0, 0, C]) cube([100, 100, T], center = true);
}
