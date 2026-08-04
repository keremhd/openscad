# Controller brief — fillet feature

You are the controller. You route work to subagents and hold the thread. You do not do the work.

## Protect your context

There are ~1500 lines of handoff, six defect records of 26–56 KB, and four review reports. You
cannot hold them and still be useful at the end.

- Delegate every investigation. Require **conclusions and numbers only** — no file contents, no
  build logs, no diffs. A subagent returning more than ~60 lines has failed its brief; re-ask.
- Each subagent writes its full record to a file and commits it. You keep the path, not the text.
- Never run a build or a sweep in your own context.
- Read only these sections of `handoff-2026-08-03.md`: *OWNER DIRECTIVE*, *Standards the owner
  set*, *READ THIS FIRST*, *Remaining work, in order*. Delegate the rest.

## State

Branch `kerem-fillet`, one worktree, clean. Landed: D21, D20, D17.3. D18 closed. D19 parked at
tag `d19-wall-recognition`. **Suite is 1709 assertions / 85 cases, green.** Hold that number.

Worktrees were deleted after the integration. Read the records from git, do not check them out:

| record | command |
|---|---|
| integration, 805 lines | `git show integ-three:work/INTEGRATION.md` |
| D20 | `git show worktree-agent-a1cc15393bdb9006e:work/NOTES.md` |
| D21 | `git show worktree-agent-ad442fb5e2bfb0c44:work/D21.md` |
| D17.3 | `git show fix-blockers:work/BLOCKERS-AB.md` |
| D19 | `git show worktree-agent-a60c66ea1a5277bb6:work/D19.md` |
| `Chain::verts` audit + patches | `git show audit-chain-verts:work/CHAIN-VERTS-AUDIT.md` |
| the four reviews | `fillet-feature-design/reviews/*.md` |

## Order of work

Do not reorder without telling the owner why.

1. **Strip every env var** — `LOCALGROUP`, `SEAMOVER`, `SIZEGATE`, `RESAMPLE_DEBUG`,
   `SIZEGATE_DEBUG`. Each mechanism ships on unconditionally or does not ship.
   - **Run the 13/13 byte-identity control before the gate goes** — it can only be run while the
     gate exists.
   - `FilletBuilder_test.cc:55–72` is the only fixture; it deletes outright.
   - D21's two hermetic tests are written against `ScopedSizeGateRule` and the `legacy` value and
     **will not compile** once `SIZEGATE` goes. Its scale-invariance test takes its expectation
     from *similarity*, not from output, so it survives with only the guard removed. **The
     removal must not take that test with it.**
   - Re-run: the suite in every environment, the D21 family, the 144-run box census.
2. **Apply the `Chain::verts` audit patches, then rename `verts` → `stations`** and take R1/R2/R3
   from the audit. The same fault was fixed three times in one integration; the rename makes the
   compiler find the rest.
3. **Build a default-settings corpus before attempting any new fix.** Every model in the existing
   225-model corpus writes an explicit `$fn` and nobody ever rendered a junction — which is how
   D22, D23 and D24 survived an effort that measured genus on 225 models. The new corpus must
   include models with **no `$fn`**, and the pass must include looking at junction renders, not
   only genus counts.
4. **D22** — the crease threshold cannot see `$fs` (`src/core/CurveDiscretizer.h:52`). Highest
   user impact: stock defaults, commonest shape in OpenSCAD.
5. **D24** — a bead whose neighbour was refused is truncated and left open. All-planar repro, so
   it needs neither curvature nor D22.
6. **D23** — whole creases discarded on misses that are not real. Look hard at **splitting a
   refused chain** rather than at correcting the miss. Do not start from the "equal radius"
   framing; that was the first write-up and it is wrong.

## Rules that earned their place — put these in every subagent brief

- **One adversarial reviewer per implementer.** Roughly a dozen blockers in this effort, every
  one found by a reviewer and none by the implementer.
- **Check the feature actually fired before believing a win.** One reported win was byte-identical
  inertness. Report served / resample / refusal counts beside every number.
- **Run any new metric on a case whose answer is already known.** Six broken instruments so far.
- **Verify the binary's mtime is newer than your last edit.** `make` reports "Built target" for
  binaries that do not exist. The GUI target is **`OpenSCADExe`**, not `OpenSCAD`.
- **`$fa` defaults to 12**, so the crease threshold is 18°. Set `$fa=360/$fn; $fs=0.01;` in
  measurement models and confirm the threshold moved — except when testing D22, where the default
  path is the subject.
- **15 of 225 corpus models are nondeterministic in vertex order.** "Byte-identical" is strong
  evidence; "differs" may be noise — re-run before calling it a regression.
- **Commit as you go.** Eight watchdog kills and two power losses; every agent resumed from its
  transcript, but only committed work survived. Bounded batches, stream output, checkpoint rows.
- Set `CCACHE_BASEDIR=/Users/kerem/Devel/openscad` before configuring any new worktree.
- Treat other agents' directories as read-only; delete only what you created.

## Reporting

After each task, one message to the owner under 30 lines: what changed, the suite number, the
headline measurement, and **what you did not measure**. State gaps rather than leaving them
silent. Nothing is pushed; do not push without asking.
