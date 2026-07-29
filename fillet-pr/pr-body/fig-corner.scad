// Figure: junctions and corner cells.
//
// Where three or more selected creases meet, each incident bead is truncated
// short of the vertex and a corner cell fills what they vacated. The cell is
// hulled from the seated ball at the junction and the sections the beads stop
// at, so the three beads and the corner leave one continuous surface.
//
// Left  the tool solid for a cube: twelve beads and eight corner cells
// Right the same tool subtracted from the cube it came from

$fn = 32;

module block() cube([20, 20, 20]);

round_tool(r = 5) block();

translate([30, 0, 0]) difference() {
    block();
    round_tool(r = 5) block();
}
