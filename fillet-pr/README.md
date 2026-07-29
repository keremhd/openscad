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
         --imgsize=1100,620 --projection=p --colorscheme=Cornfield \
         -o fig-name.png fig-name.scad
```

Two exceptions: `pr-body/fig-classify.png` is a preview render (drop `--render`),
because the debug overlay is a cloud of disjoint marker cubes rather than a
solid; and `doc-page/fig-brush-one-edge.png` uses `--imgsize=1200,400` to suit
its three panels.

Every image was last rendered against the tree at `b6e6bbec3`, the corner-cell
rework. Re-render after any change to the builder, and check the piece count as
well as the picture: a figure that cuts a model open can leave bead ends floating
if its cutting box is not generous enough, which looks exactly like a tool
shedding crumbs and is not.

The figure scripts are also worth keeping as review material in their own right —
each one is a small worked example with the reasoning in its header comment.
