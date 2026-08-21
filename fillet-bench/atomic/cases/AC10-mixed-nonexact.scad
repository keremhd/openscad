// atomic-case:      AC10
// atomic-title:     mixed corner outside the torus gate: 40 deg rake on a 120 deg trough
// atomic-family:    mixed-corner
// atomic-class:     tube
// atomic-select:    both
// atomic-camera:    6,3.36,6 | -1,-1,0.7 | 60
// atomic-params:    R=2 CT=2
//
// atomic-expect:    fn=*   op=fillet   tube=4 capTri=12 weld=26 *=0
// atomic-expect:    fn=19  op=fillet   tube=4 capTri=12 flat=1 weld=25 *=0   one weld turns ear-clip
// atomic-expect:    fn=32  op=fillet   tube=4 capTri=12 flat=1 weld=25 *=0   the same, and it stays
// atomic-expect:    fn=64  op=fillet   tube=4 capTri=12 flat=1 weld=25 *=0   the same, and it stays
// atomic-expect:    fn=*   op=chamfer  fan=16 weld=26 *=0
// atomic-tier:      fn=*   op=fillet   tier=pullIn
// atomic-tier:      fn=*   op=chamfer  tier=baseline
// atomic-mesh:      fn=*   op=*        VALID folds<=2 warn=0   two darts at the raked end
// atomic-measured:  9de6d8c0 2026-08-21
//
// atomic-history:   NEW. The corpus violates the gate one way at a time -- an oblique
//                   end face (elbow_oblique50) OR a non-square trough (elbow_dihedral60)
//                   -- and both measured back as tube=2 once the canal corner landed
//                   (75cb8e279). Violating both at once is what was expected to leave
//                   the exact surface behind and reach the Coons membrane, which has no
//                   tile anywhere in the bench. It does not: this measures tube as well.
//                   Three earlier framings of this case were thrown away because the rake
//                   plane grazed a face instead of cutting it -- at 24x18 with R=2 a rake
//                   that exits within ~0.2 mm of the leaning wall's base leaves a sliver
//                   the operator refuses (9 sharp crease edges, warn=1). The rake here
//                   exits the near face at x=10, between the crease at x=6 and the wall
//                   base at x=12.93, and the model is clean.
// mesh.py-comp:     1
//
// The wall leans up-RIGHT out of the crease with the floor on its left, so the trough
// is a 120 degree dihedral. The polygon is written in the x-z plane and extruded along
// y so the wall thickness is a true perpendicular 6 mm (rotating a box instead buries
// its base and adds edges that have nothing to do with this corner). The concave crease
// is the line x = 6, z = 6. The rake plane is y = -x*tan(40) + 8.39, so the crease dies
// into it at y = 3.36 -- the camera centre, read off the source and not eyeballed.
FNSET = 0; $fn = FNSET;
R  = 2;
CT = 2;
OP = 1;

module solid() {
    intersection() {
        rotate([90, 0, 0]) translate([0, 0, -18]) linear_extrude(18)
            polygon([[0, 0], [24, 0], [24, 6], [12.93, 6],
                     [19.196, 16.86], [14, 19.86], [6, 6], [0, 6]]);
        rotate([0, 0, -40]) translate([-60, 6.43, -10]) cube([140, 140, 50]);
    }
}

if      (OP == 0) solid();
else if (OP == 1) fillet(r = R)   solid();
else              chamfer(t = CT) solid();
