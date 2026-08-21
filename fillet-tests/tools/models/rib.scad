// A rib on a plate: the base ring is four concave creases meeting at four turns,
// and its bare tool is one piece of genus 1 -- a closed loop, which is the check
// worth making on it.
module m() {
    cube([56, 44, 9]);
    translate([6, 6, 0]) cube([9, 30, 24]);
}
