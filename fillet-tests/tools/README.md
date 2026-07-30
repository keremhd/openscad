# tools — measuring blend results instead of looking at them

Small, dependency-free helpers written while chasing D11 and D12. They exist
because the mistakes that cost the most time were all cases where a picture, or a
single number, said the opposite of the truth.

Stdlib Python 3 and `/bin/sh` only. Nothing here is part of the build.

| | |
|---|---|
| `meshstat.py` | connected components, per-component volume and extents of an STL |
| `pngtool.py` | PNG dimensions; join renders into a row or a grid |
| `compose-compare.sh` | run one model through every arrangement of the two blend passes |
| `models/` | the seven models worth testing any blend change against |

## The three traps these were written for

**Never judge brush extent by a bounding box.** A bead's cross-section is `r`
across however wide the brush is, so a bbox cannot distinguish a 1 mm bead from a
6 mm one. Use volume — `meshstat.py` reports it per connected component.

**Read the component count next to the volumes, never alone.** A figure that cuts
a model open leaves bead ends floating if its cutting box is not generous, which
looks exactly like the tool shedding crumbs and is not. Equally, twenty
components of 4e-6 mm³ is a shattered result that a genus check can miss.

**`--imgsize` is part of a figure, not a detail of it.** `--viewall` fits the
model to whatever canvas it is given, so re-rendering a set at one "documented"
size letterboxes every figure composed at another. `pngtool.py dims` is how you
notice.

And one that is not about geometry: **do not difference two near-coincident
solids to measure how much they disagree.** On the L bracket at `$fn = 64` that
produced 40–60 shards of ~0 mm³ and, on one run, identical numbers for two
different inputs because the STL had not been rewritten. Render and look, or
measure a specific quantity.

## Usage

```sh
./meshstat.py result.stl                  # one line per file
./meshstat.py --pieces result.stl         # one line per component, with extents

./pngtool.py dims fig-*.png
./pngtool.py row out.png a.png b.png --key red,green
./pngtool.py grid out.png a.png b.png -- c.png d.png --key green,red

./compose-compare.sh models/bracket.scad 2 2 32
OPENSCAD=/path/to/openscad ./compose-compare.sh models/tee.scad 5 1 64
```

`compose-compare.sh` takes a model defining `module m()` and runs it through five
arrangements — `today` (both tools from the original child), `chained` (the outer
pass on the solid the inner pass produced), `brushed` (the inner pass brushed out
of the outer pass's region, using that pass's own tool solid as a negative
brush), and each pass alone — reporting genus, warning count, components and
volume together, because no one of those catches every failure.

**It measures the binary, not the branch.** The arrangements differ only where a
bead meets a rounded edge, which is exactly what the builder keeps changing, so a
table taken against a stale `build/` describes a tree that no longer exists.
Rebuild before believing a row, and record the commit next to any numbers kept.

## The models

`bracket` is the one most changes show up on first: one reflex crease, and a
convex chain running each end face's outline that turns 90° at the reentrant
corner. `tee`, `bosses` and `dome` are the curved cases and are where anything
reasoning about blend surfaces goes wrong — the tee worst, because its weld
fillet is a closed crease whose angle changes at every point. `boss` and `bore`
are the closed-ring cases that most changes leave untouched, which makes them
good controls. `rib` has a base ring of four concave creases meeting at four
turns, and its bare tool should be a single piece of genus 1 — a closed loop.

A useful habit: run a change against all seven and expect most of them to come
back *byte-identical*. A narrow change that alters every model is not narrow, and
finding that out costs one command.
