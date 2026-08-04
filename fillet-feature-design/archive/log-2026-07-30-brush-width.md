# 2026-07-30 — BRUSH-WIDTH, and D9 and D10 re-measured

BRUSH-WIDTH landed as one rule with two halves, and the two builder tests it
asked for. Before it, D9 and D10 were re-run against what each had been asked to
do rather than against the log it left, because both closed with a "the item
cannot be met as written" note and those are the notes worth checking.

## The rule, and where it differs from the table in the item

The item proposed deciding a selected stretch by its endpoints, in three rows:
covered whole (build it at any length), reaching one chain end (length matters
for the corner there), a window strictly inside (debounce only). Two of the three
are implemented as written. The middle one is not, and the difference is the
debounce:

| ends the brush cut | rule as implemented |
|---|---|
| neither | build it, no length test at all |
| one | debounce, and drop it if it reaches a corner without covering `r` |
| both | debounce |

**The debounce belongs to any stretch the brush cut, not only to one it cut at
both ends.** The item argues that a chain-ended interval "cannot be that
artefact", and there is a test in the tree that says otherwise: `brush: a graze
too short to be a bead is dropped` grazes the spine with a 0.001 mm slab that
happens to straddle the crease's own start vertex, so the stub it leaves runs
from that vertex and has exactly one brush-cut end. It is still a brush face
crossing the spine twice a hair apart, and it still leaves half a micron of bead.
One cut end is enough to ask the question; no cut end is what makes the question
meaningless, and that is the row the reported bug is in.

So the exemption is scoped to a stretch the brush cut nowhere, which is the
reported bug exactly:

```openscad
module part() { cube([1000, 5, 2]); cube([5, 1000, 2]); }
fillet_tool(r = 400) { part(); translate([-9000,-9000,-9000]) cube(18000); }
```

The one concave crease is 2 long against a debounce of `0.01 * 400` = 4. Both
invocations — with the containing brush and without any brush — now write the
same STL, byte for byte, and the second reports `takes 1 of 1 candidate edge(s)`
where it used to claim the brush covered none of them.

## Row 2: what a corner does with a stretch that falls short of it

`dropUncoveredCorners` replaces `uncoveredCorners`. Same question, and now it
acts on the answer: at a vertex three or more selected ends land on, where fewer
than three cover `r` of crease back from it, no cell is built — and every stretch
that runs into that vertex short of `r` is dropped with it. A bead ending inside
the corner it was asked to close meets nothing there, so the honest answer to
"round this corner with a box smaller than the radius" is nothing rather than
three stubs. On `cube(20)` at `r = 3`, a box reaching 2 mm past the top vertex
now builds an empty tool; at 3 mm and above nothing changed.

D9 left the beads deliberately and said the warning would have to change when
this landed. It did, and it also had to be **scoped**, which the item did not
anticipate:

- A corner where *nothing* was covered is a brush drawn around a corner and
  answered with silence. It warns, naming the vertex and the radius.
- A corner where something was covered is the single-edge recipe: a brush along
  one edge clips the four neighbours at the shared vertices, and those stubs are
  dropped by this same rule. It is silent. BRUSH-WIDTH's own text says not to
  warn there — "a warning would fire on the accidental case the rule was written
  for" — and without the scoping the documented recipe emits two warnings.

That scoping is what makes the recipe writable with a brush a model can draw. At
`r = 3` on `cube(20)`, a column straddling one vertical edge over its full height
takes 1 of 12 at every width up to just under 6 mm, and 5 of 12 at 6.5 — the
limit being how far it reaches *from* the edge, against the radius. The 0.02 mm
column the doc page used is no longer load-bearing; it is now one width among
many, and the page says so.

`minLength` is renamed `debounce` at both ends, and the comment at the constant
says it is a debounce and nothing else — in particular that nothing else rests on
its value now, since it is the corner rule and not this constant that makes the
one-edge selection reachable. That was the one way this could have gone wrong
later: the previous comment invited someone to retune it and quietly break a
documented recipe.

## Tests

- `brush: a crease selected end to end is kept however short it is` — the L
  above, at the builder level: the containing brush leaves the chain carrying no
  intervals, and the two tools have the same volume. Fails with the exemption
  removed.
- `brush: width selects the crease and does not shape the blend` — the same 10 mm
  of one edge taken by a 0.02 mm hair and an 8 mm slab, same volume to 1e-9, same
  square caps. This is the property the doc page states and nothing held.
- `brush: one whole edge and only that edge is a brush a model can draw` — 1 of
  12 at 0.02 and 2.9 and the full height blended, 5 of 12 at 8, and nothing
  reported at either vertex. Takes 5 with the drop disabled.
- `brush: a corner the brush reaches but does not cover is not built` absorbed
  `brush: a selection swallowed by the junction setback builds no bead`. The two
  had converged on the same configuration — cube, `r = 3`, a brush reaching less
  than `r` past one vertex — and now have the same answer, so what was two tests
  is one, keeping D7's assertion that the emptied selection is not read as "no
  brush" and D9's junction count and reported corner.

`ctest -R "fillet|round|chamfer|bevel"` is green (76), the unit suite is green
(72 cases), and `fillet-tests/run_all.sh` matches expectations (82 checks).
`case_brush_halfchain` is untouched: its brush cuts the crease at one end only,
and that end is a free end of the chain with no corner at it.

## D9 re-measured

Correct, and the log's excuse holds. What was checked, on `cube(20)` at `r = 3`
with a box reaching `D` past the top vertex:

- **The brush contract along a spine is exact**, which is the claim the item's
  own acceptance table could not show. Built one chain at a time, each bead spans
  exactly `[0, D]` along its own crease at `D` = 0.5, 1, 2, 3 and 6, and its
  volume is linear in `D` to five figures. The confounding the log describes is
  real: three beads meeting at a corner each have an `r`-wide section across the
  other two directions, so the tool's bounding box reads about `r` in every
  direction whatever `D` is, and so does the box of any one bead. A tool-minus-
  the-other-two subtraction is confounded too — it leaves slivers along the
  coincident surfaces — which is what made this look wrong at first.
- **The corner decision is where the log says.** No junction below `D = r`, one
  at and above it.

The volume table in the log does not reproduce as printed: at 24 arc segments
these read 1.68, 1.70, 13.52 and 31.69 against the log's 2.06, 2.08, 14.83 and
35.59. The ratios and every conclusion are the same, and the row that matters
(13.52 as the "before" at `D = 1`, which is the whole cell) is exactly the "after"
at `D = 3`. Different tessellation, not a different tool — but the table should
have said which.

## D10 re-measured

Correct, pinned, and the cause is the one written down. With `kEdgeSlack` set to
zero the test `brush: a spine down the middle of a brush face is still cut by it`
fails on its first width with zero crossings where it needs one, so the fix is
genuinely held.

The log's claim that there is no threshold is right and its test comment did not
say so. Extending the widths with the slack removed:

| `W` | 0.02 | 0.1 | 0.2 | 0.25 | 0.3 | 0.35 | 0.4 | 0.45 | 0.49 | 0.5 | 1 | 2 | 4 | 8 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| crossings | 0 | 0 | 0 | **1** | 0 | **1** | 0 | 0 | **1** | 1 | 1 | 1 | 1 | 1 |

Not monotonic, exactly as the log says. The comment in the test said "every one
at or below 0.4 lost it and every one above kept it", which is true of the seven
widths it loops over and reads as a threshold to anyone who does not extend it —
the reading the log spent an hour disproving. Reworded to say the widths are a
spread rather than a boundary.
