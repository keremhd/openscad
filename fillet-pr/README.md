# fillet-pr — throwaway

**Delete this whole directory before the PR merges.** Nothing in it is part of
the feature; it is the writing that goes *around* the PR, staged here so it can
be reviewed and revised in the branch instead of being pasted into a web form and
lost.

Three directories, one markdown file each, with the `.scad` sources for every
figure and the `.png` they render to beside them.

| Directory | File | Where it ends up |
|---|---|---|
| `pr-body/` | `pr.md` | pasted into the pull request description |
| `wiki-page/` | `fillet-round-chamfer-bevel.md` | the public OpenSCAD wiki, images uploaded there |
| `doc-page/` | `fillet.md` | **moves into `doc/` before the merge** — this one is the only content here that survives, and it survives by being moved, not by this directory staying |

The other two directories to remove at the same time are
`fillet-feature-design/`, which is the design and milestone record, and
`fillet-tests/`, the prototyping bench. Both are kept until PR review is settled:
the design directory answers "why is it built this way", and `fillet-tests/`
draws the six-column reference diff, which is what to reach for when a reviewer
asks whether a particular corner is actually right.

## Regenerating the figures

Every image here is rendered from the `.scad` beside it:

```sh
openscad --backend=manifold --render --viewall --autocenter \
         --imgsize=<W>,<H> --projection=p --colorscheme=Cornfield \
         -o fig-name.png fig-name.scad
```

**`--imgsize` is per figure and is not a detail.** `--viewall` fits the model to
whatever canvas it is given, so a height chosen for one figure letterboxes
another: render the three-panel figures at 620 tall and the panels shrink into a
band of empty background. The sizes below are the ones each figure was composed
at. Re-render with the figure's own size, not a single default — and check the
pixel dimensions afterwards, because a wrong `--imgsize` produces a picture that
is not obviously wrong on its own, only worse.

`pr-body/fig-classify` is also the one **preview** render — drop `--render` —
because the debug overlay is a cloud of disjoint marker cubes rather than a solid.

Then check the piece count as well as the picture: a figure that cuts a model open
can leave bead ends floating if its cutting box is not generous enough, which
looks exactly like a tool shedding crumbs and is not. Count the pieces, and
measure them by **volume, not bounding box** — a bead's cross-section is the same
`r` across however wide the brush is, so a bounding box cannot tell a 1 mm bead
from a 6 mm one; volume can.

Everything below was measured against the tree at `5121d87a5`, the fix for a
spine that turns from one wall onto the next. One piece per panel, unless the
panel is a marker cloud:

| Figure | `--imgsize` | Pieces | What they are |
|---|---|---|---|
| `pr-body/fig-wedge` | 1200,460 | 3 panels | the wedge by hand, the wedge minus a cylinder, and `fillet_tool`'s own answer — the last two should look identical, which is the figure's point. Copied unchanged into `wiki-page/` and `doc-page/` |
| `pr-body/fig-ball` | 1300,420 | 4 panels | the seated ball, the cylinder it sweeps, the finished blend, and one ball in a corner |
| `pr-body/fig-classify` | 1100,620 | 225 | disjoint marker cubes, so a cloud is correct |
| `pr-body/fig-corner` | 1100,560 | 2 | the bare tool for a cube — **one** piece, which is the figure's point — and the cube it is cut from |
| `pr-body/fig-hard-cases` | 1200,520 | 3 | one per panel; the pocket's cutting box is deliberately oversize, see the note in the script |
| `doc-page/fig-brush` | 1100,600 | 2 | the two brush variants |
| `doc-page/fig-brush-one-edge` | 1200,400 | 3 | the three panels |
| `doc-page/fig-fillet` | 1100,560 | 4 | untouched, both halves, convex only, concave only |
| `doc-page/fig-min-angle` | 1100,560 | 2 | the two thresholds |
| `doc-page/fig-tools` | 1100,560 | 8 | four bare tools in the back row, four composed in the front |
| `wiki-page/fig-curved` | 1100,560 | 3 | the three curved cases |
| `wiki-page/fig-quickstart` | 1100,560 | 2 | the bracket as written and the same one filleted |
| `wiki-page/fig-selective` | 1100,600 | 1 | one cutaway part |

`fig-wedge` and `fig-ball` are newer than that measurement and were rendered
against `d615bac2b`; their two "pieces" columns say what the panels are rather
than a count, because both figures colour the model and the tool separately.

Re-rendered at those sizes against `5121d87a5`, every figure but
`wiki-page/fig-quickstart` comes back **byte-identical** to its render against
`7cd6a9037`, and that one differs only because its `.scad` was rewritten. Nothing
in the turn fix changed any existing figure's geometry, which is the check worth
repeating: if a builder change is meant to be narrow, byte-identical renders are
what says so.

The figure scripts are also worth keeping as review material in their own right —
each one is a small worked example with the reasoning in its header comment.
