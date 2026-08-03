# The 13-control byte-identity check, re-run against the current tree

Taken 2026-08-04 while `OPENSCAD_FILLET_SIZEGATE` still exists, so that the
`legacy` column can be taken at all. Nothing here changes any source file.

## What the control is

Defined in `git show integ-three:work/ab/controls.sh`, reported in
`git show integ-three:work/INTEGRATION.md` under "The controls, correctly
restated" and in `work/BLOCKERS-AB.md` as "**13/13 controls byte-identical**".

Thirteen models — `ctrl1 ctrl2 ctrl3 ctrl4 ctrl5 boss grid rib pocket rhomb
slab corner grid100` — exported to STL with the seam environment unset, and the
bytes compared against a clean `944e0cbef` build.

Recorded results:

* D17.3 in isolation (`work/BLOCKERS-AB.md`): **13/13 BYTE-IDENTICAL**.
* The integration tree (`work/INTEGRATION.md`): **12/13, `rib` differs** — and
  the report attributes the deviation to D20 and D21, not to D17.3, since
  `three` and `merged` are byte-identical on `rib`.

## Binaries

| tag | binary | mtime |
|---|---|---|
| `head` | `.../12d3d7aa-.../scratchpad/bins/head/OpenSCAD.app/Contents/MacOS/OpenSCAD` | 3 aug 11:46:50 2026 |
| `cur` | `/Users/kerem/Devel/openscad/build/OpenSCAD.app/Contents/MacOS/OpenSCAD` | 4 aug 00:24:11 2026 |

`cur` is `kerem-fillet` at `5ae7e4a1f4b3557d6e9b8443033daf5f7a26bcff`, working
tree clean. Last source edit in `src/geometry/fillet/` is
`FilletBuilder.cc` / `FilletBuilder_internal.h` / `FilletBuilder_test.cc` at
**4 aug 00:16:15 2026** — the binary is 00:24:11, i.e. **newer than the last
source edit**. `make OpenSCADExe` re-run before measuring and reported
`[100%] Built target OpenSCADExe`; the binary was confirmed to exist by `ls`
(24308792 bytes), not by trusting the "Built target" line. Target is
`OpenSCADExe`, not `OpenSCAD`.

`head` is the same clean-`944e0cbef` binary the integration report used, taken
read-only from another agent's scratchpad; nothing in that directory was
written or deleted. It was verified to predate all three changes rather than
trusted: `strings -a` on it yields **zero** `OPENSCAD_FILLET_*` symbols, while
`cur` yields `LOCALGROUP`, `RESAMPLE_DEBUG`, `SEAMOVER`, `SIZEGATE`,
`SIZEGATE_DEBUG`.

Both binaries were run from inside their own `.app` bundles, so localisation
initialises identically for both — this machine's locale uses comma decimals
and a bundle-vs-loose binary could have formatted STL coordinates differently.

## Commands

Runner: `scratchpad/run13.sh` (bash, not zsh — `env $VAR ...` in zsh performs
no word splitting and would silently drop the second variable). Per model:

```
env -u OPENSCAD_FILLET_LOCALGROUP -u OPENSCAD_FILLET_SEAMOVER -u OPENSCAD_FILLET_SIZEGATE \
    $HEADB -o h_$m.stl $m.scad
env -u OPENSCAD_FILLET_LOCALGROUP -u OPENSCAD_FILLET_SEAMOVER -u OPENSCAD_FILLET_SIZEGATE \
    OPENSCAD_FILLET_SIZEGATE_DEBUG=1 $CUR -o c_$m.stl $m.scad
env -u OPENSCAD_FILLET_LOCALGROUP -u OPENSCAD_FILLET_SEAMOVER \
    OPENSCAD_FILLET_SIZEGATE=legacy $CUR -o l_$m.stl $m.scad
cmp -s h_$m.stl c_$m.stl ; cmp -s h_$m.stl l_$m.stl
```

Model sources extracted from `git show integ-three:work/ab/<m>.scad`; no
worktree was checked out. Each row was appended to a file as it was produced.

Counts come from `FILLETGATE` diagnostic lines on the `cur` default run:
`gate` = chains reaching the size gate, `fits` = `verdict=0`, `blind` =
`tested=0 && exempt>0`, `blindrefusal` = blind and `verdict!=0`.

## Result — two passes, identical

| model | cur vs head | `SIZEGATE=legacy` vs head | gate chains | fits | blind | blind refusals |
|---|---|---|---|---|---|---|
| ctrl1 | BYTE-IDENTICAL | BYTE-IDENTICAL | 1 | 1 | 0 | 0 |
| ctrl2 | BYTE-IDENTICAL | BYTE-IDENTICAL | 4 | 4 | 0 | 0 |
| ctrl3 | BYTE-IDENTICAL | BYTE-IDENTICAL | 12 | 12 | 0 | 0 |
| ctrl4 | BYTE-IDENTICAL | BYTE-IDENTICAL | 1 | 1 | 0 | 0 |
| ctrl5 | BYTE-IDENTICAL | BYTE-IDENTICAL | 2 | 2 | 0 | 0 |
| boss | BYTE-IDENTICAL | BYTE-IDENTICAL | 1 | 1 | 0 | 0 |
| grid | BYTE-IDENTICAL | BYTE-IDENTICAL | 36 | 36 | 0 | 0 |
| **rib** | **DIFFERS** | **DIFFERS** | 3 | 1 | 1 | 1 |
| pocket | BYTE-IDENTICAL | BYTE-IDENTICAL | 4 | 4 | 0 | 0 |
| rhomb | BYTE-IDENTICAL | BYTE-IDENTICAL | 4 | 4 | 0 | 0 |
| slab | BYTE-IDENTICAL | BYTE-IDENTICAL | 4 | 4 | 0 | 0 |
| corner | BYTE-IDENTICAL | BYTE-IDENTICAL | 2 | 2 | 0 | 0 |
| grid100 | BYTE-IDENTICAL | BYTE-IDENTICAL | 100 | 100 | 0 | 0 |

**12 of 13 byte-identical to clean HEAD, default environment.
12 of 13 with `OPENSCAD_FILLET_SIZEGATE=legacy`.**

Totals across the 13: **174 chains reached the size gate, 172 fitted, 1 blind
chain, 1 blind refusal** — all on `rib`. So on twelve of the thirteen controls
the gate had a testable sample for every chain and refused nothing; the whole
of the mechanism's visible activity on this corpus is the single blind refusal
on `rib`.

### Re-run, because "differs" may be noise

The 15-of-225 nondeterminism class was ruled out by running the whole control
set twice end to end. `rows_pass1.txt` and `rows_pass2.txt` differ only in the
literal string "pass 1"/"pass 2": **all thirteen rows, including all 39 md5
prefixes, are identical between passes.** None of the 13 controls is in the
nondeterministic class, and `rib`'s difference is real, not vertex-order noise.

### What `rib` actually does

Three distinct meshes, pairwise different (`head` `48c37713`, `cur` `4af898e7`,
`legacy` `8977c016`):

| build | verts | tris | components | non-manifold edges | genus | volume |
|---|---|---|---|---|---|---|
| head | 4917 | 9830 | 1 | 0 | 0 | 21111.710287 |
| cur (default) | 4276 | 8548 | 1 | 0 | 0 | 21105.659735 |
| cur `legacy` | 5300 | 10596 | 1 | 0 | 0 | 21111.884913 |

All three are sound closed single solids. The spread in volume is 2.6e-5
relative — the difference is how far the bead extends at the refused rib
crease, not a soundness regression.

`SIZEGATE=legacy` does **not** restore the clean-HEAD mesh; it produces a third
one. That is the measurement that could only be taken while the env var exists,
and it confirms the integration report's attribution: reverting D21's gate
alone does not put `rib` back, so the deviation from clean HEAD is not D21's
alone — D20 is moving this mesh too. It also shows the gate is live on `rib`
under the default and quiescent under `legacy`.

## Verdict

**The control reproduces the integration report exactly: 12/13, `rib` the one
that differs.** It does not reproduce D17.3's isolated 13/13, and was not
expected to — byte-identity to clean HEAD cannot survive two default-on changes
(D20 and D21), which is the report's own conclusion. The right form of this
control going forward is byte-identity against the same tree *without* the
change under test.

## What was not measured

* No new clean-`944e0cbef` build was made; the baseline is the integration
  agent's `head` binary, used read-only and verified only by symbol absence.
* No D20-alone or D21-alone build, so `rib`'s difference is not split between
  the two changes beyond what the `legacy` column shows.
* `three` vs `merged` byte-identity on `rib` (the report's proof that D17.3 is
  inert with its flag off) was not re-taken — no `merged` binary was built.
* Only these 13 models. Not the 144-run box census, not the 81 shapes, not the
  225-model corpus, not any `$fn` or scale sweep.
* No `LOCALGROUP=1` / `SEAMOVER` runs; both were explicitly unset throughout.
* No resample counts: `OPENSCAD_FILLET_RESAMPLE_DEBUG` was not enabled on these
  runs, so the `rd` figures that appear elsewhere in the record have no
  counterpart here.
* No unit tests were run.
