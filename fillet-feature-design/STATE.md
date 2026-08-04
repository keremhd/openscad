# State of the fillet feature — 2026-08-04

A description of where the work stands, not a plan. Written to be forked from: it says what is
true, what is proven and by what evidence, and what is believed but unproven.

Branch `kerem-fillet`. **Nothing is pushed.** Suite **2230 assertions / 88 cases**.

The recorded pin of "1709 / 85" was stale for some time before it was noticed: the tree
measured 2224 / 86 by stash on 2026-08-04, so the figure the gate was written against had not
been true for a while. A pin nobody re-measures is a comment, not a check.

**This file is amended in place. Do not start a new dated state or handoff document** — the
dated chain grew 392 → 778 → 1421 lines and its cost is what retired it.

Read alongside it: [`ACCEPTANCE.md`](ACCEPTANCE.md), what the feature promises and the gate it
ships against — that is the authority on whether something is work or a release note; and
[`TRAPS.md`](TRAPS.md), the environment traps, the nine broken instruments and the corrections
to the record. Everything superseded is under `archive/` and stays in git regardless.

---

## 1. What is true of the code right now

**No environment variables.** All five are gone — `OPENSCAD_FILLET_LOCALGROUP`, `SEAMOVER`,
`SIZEGATE`, `RESAMPLE_DEBUG`, `SIZEGATE_DEBUG`. Each mechanism either ships unconditionally or was
deleted:

| mechanism | shipped state |
|---|---|
| local grouping (D17.3) | **on**, unconditionally |
| seam overrun | the constant **0.10·r** |
| `seamRoom` / retreat fallback | on (an earlier review's "delete three mechanisms" conclusion is recorded as wrong) |
| size gate blind-crease branch (D21) | **on** (`asksBlindCreases == true`) |
| `arrivesStraight` | **deleted 2026-08-04** — see §4 |
| resample and gate diagnostics | deleted |

**The crease threshold is a constant, `kDefaultCreaseThresholdDeg = 46.0`**
(`FilletBuilder_internal.h`). No render variable and no mesh statistic is read; `min_angle`
overrides it as before. `seamAngle()` and its clustering, `creaseThreshold()`, and
`buildFilletTool`'s `thresholdOverride` parameter are all gone, as is the workaround that
measured `fillet()`'s threshold once on the child and handed it to both passes — that was
C++-only privilege a SCAD author could not express, and `fillet()` is required to be pure
sugar over the four tool modules. `CurveDiscretizer::getMaxSeamAngle()` now has **no
production caller**; only a test helper uses it.

`SEAT` / `WALLFACE` never had a reader on this branch; D19 is parked at tag `d19-wall-recognition`.

**`Chain::verts` is now `Chain::stations`, and private.** The audit's patches are applied and R1/R2/R3
taken. The rename surfaced 58 references, of which **three were genuinely wrong** — a test walking
every station of a closed chain into `m.pos`; a seam-truncation loop indexing the station list with a
length from `sections[ci].size()` that is only equal inside that window; and `ChainContact::vert`
holding an interior station index, write-only, deleted.

**The end-vertex guarantee holds without asserts.** `endVert` is replaced by
`bool openEnds(int& front, int& back)`, which returns false for a closed chain. `endVert` has zero
occurrences left in `src/` or `tests/`. Eleven duplicated call-site `closed` guards were folded in
(nine entirely; two keep their `sections[ci].size()` half).

---

## 2. Evidence, and how strong each piece is

**Proven, independently reproduced:**
- Env-var removal is a faithful flag-flip: all 16 diff hunks are constant-folds of the gated-on path,
  verified by source at `-U0`, so equivalence holds for **every** model, not only those measured.
- 225-model corpus, both arms run twice: **4 real movers, none worse**, one better
  (`boss_on_plate_fn192`, non-manifold edges 88→40, genus −44→−20). Unsound count 7 of 225 before and
  after, same list.
- Census counts identical either side: 300 refusals, 498 blind chains, 0 blind refusals, 54
  re-divided. Shipped binaries select 7428 creases and refuse 6887 both sides. **The feature fires; it
  stopped asking permission.**
- The `stations` rename and the `openEnds` refactor are byte-identical on 19 models — the second
  re-taken independently after the first agent was killed twice, and stronger than md5: the entire
  console output matches, every `ECHO: fillet_tool:` count included.
- `openEnds` refuses where it must: a probe fired **1139 times, all `closed=1`**, 1135 from
  production code.

**Proven, single-source (believed, not reproduced):** F6's 109 ring closing edges (re-measured once by
a reviewer with its own probe, `grid100`=100 predicted in advance, `rib`=0).

**Unproven and known to be so:**
- The `size() < 2` half of the `openEnds` guard **never fired** (0 of 1139).
- No clean-`944e0cbef` baseline exists anywhere in this session's work — every comparison is
  pre-change vs post-change.
- Manifold backend only. No CGAL, no `$fn` or scale sweeps, no fold census, no D19 ledge, no D17.3
  oblique sweep.

---

## 3. Structural facts worth not rediscovering

- **`stations` privacy is what prevents the recurring fault**, not vigilance. An interior station
  index cannot be named outside the class; all 23 `endVert`-era sites sat behind a `closed` guard; all
  16 `stationCount()` sites are station-space bar one. A reviewer established there is no fourth
  instance, structurally rather than luckily.
- **The old `assert(!closed)` was dead twice over** — compiled out under `-DNDEBUG` *and* unreachable,
  because eleven call-site guards already refused. What kept rings out was the guards, never the
  assertion the header credited. The `openEnds` fix is therefore **prospective**, not a live-bug fix.
- **1135 of 1139 rings had `back >= 0`**, so the pre-fix accessor would have returned a plausible
  *wrong* vertex rather than a `-1`. Worse than the failure mode R1 and the old prose described.
- **The shell-level `fillet-tests` suite is largely blind to fillet geometry** — CSG scene-graph dumps
  plus fuzzy image compares on models the seam rule does not touch. `ctest -R fillet` passing is
  necessary, never sufficient. The four models that would prove it are slated for deletion before
  merge.

---

## 4. `arrivesStraight` — deleted 2026-08-04

Record: `decisions/2026-08-04-arrives-straight-goes.md`, which supersedes `…-stays.md`.

**Two things the earlier record got wrong, both found by instrumenting rather than reading.**

First, there is no seated ball at a curved arrival. The predicate was documented as handing
the vertex back to an older seated-ball construction; instrumented at `bcurve.scad`'s two
curved-arrival vertices, `chainJunctions` finds **no junction at either**, and it is
structural — the crease that leaves such a vertex unfilleted is one a brush cut, and the same
brush withholds the corner through `noCorner`. So the vertex fell to the plain
stop-a-hair-short branch, whose own comment already describes the outcome: both beads reach
the same point of the sharp edge tangent to the wall they share, meet at no angle, and the
boolean resolves that into a knife edge. **The fin was that knife edge**, and the predicate
was choosing between the overrun and nothing.

Second, the regression that justified keeping it does not reproduce. `box_step_fn24/48/96_r3`
are byte-identical with and without the predicate, χ=2, genus 0, zero non-manifold. `box_step`
is two cubes with no curved crease, so the predicate returns true at every end and cannot act
at all. The recorded genus 0→−2, −1→−4, −5→−15 was read through the `$fa`=12 threshold
artefact: misclassified tessellation seams manufactured curved arrivals the model does not
have. **Fixing the classifier dissolved the evidence, and the decision fell with it.**

`bcurve.scad` at `$fn`=64: χ 5→2, edges on >2 faces 5→0, genus −1.5→0. Swept `$fn` 8→80 in
steps of 2, **16 of 37 tessellations invalid before, 2 after**.

**Measured and rejected:** running the overrun along the arc through the last three spine
points rather than along the chord. It removes the stated first-order departure from the wall
and measures *worse* — 19 of 37 invalid, with tunnels at `$fn` 58/64/66. Past the seam vertex
the arc's continuation is buried inside the solid the *other* bead sits on, so following it
carries the overrun away from the bead it must overlap. The chord's outward drift is what
carries it there; the apparent defect was doing the work. Building the corner ball was not
tried and should not be — an unfilleted crease leaves these vertices, which is exactly when a
ball is wrong.

## 4a. The superseded record, kept because both framings were believed

Full record: `decisions/2026-08-04-arrives-straight-stays.md`,
`measurements/2026-08-04-arrives-straight.md`, `measurements/2026-08-04-arrives-straight-renders.md`.

**What it does.** At a seam corner an unfilleted crease also leaves, the seam rule builds no corner
ball and runs the two beads past the corner along the line their last segment lies on. That line is
the crease's continuation only if the crease arrives straight. On a curve the last segment is a chord,
and producing it past its end leaves the circle. `arrivesStraight` tests the last two segments for
collinearity and, where bent, hands the vertex back to the older seated-ball construction.

**It is kept.** Removing it degrades three corpus models and improves none
(`box_step_fn24/48/96_r3`, genus 0→−2, −1→−4, −5→−15), and the damage **grows with refinement**, which
by the owner's first standard makes it a real defect rather than faceting.

**But the branch it selects is invalid.** Rendered at a curved arrival on `bcurve.scad`: the fallback
leaves a fin of two sail surfaces meeting at a cusp, with **5 edges carried by >2 faces, Euler
χ = 5 (odd), genus −1.5**. A closed orientable surface can have neither. The other arm at the same
corner is χ=2, genus 0, clean. So the predicate **chooses between two bad constructions**; it is kept
because removal measured worse, not because what it selects is sound.

**Two framings that are recorded as wrong**, so they are not re-derived from scratch:
- The D17 review's cited win (`bcurve` 0/37) is the **"ungated + global"** arm — two changes. What
  shipped is the opposite half, local grouping made permanent. "Ungated + local" was never tabled.
- `arrivesStraight`'s original justification was one model, `rib.scad`, 9/37 → 1/37. It is now
  **inert on `rib.scad`** (all 37 tessellations byte-identical) although the census proves it fires
  there. It is kept for what it protects now, which is not what it was chosen for.

**Closed by owner decision, not by measurement:** whether local grouping deserved to be permanent.
Settling it needs a `removed + global subtraction` arm; global subtraction is broken and not worth
building, so a comparison against it has no value.

---

## 5. Open defects

| defect | state |
|---|---|
| seated-bead fallback at a curved arrival | **closed 2026-08-04.** See §4. |
| D22 — crease threshold reads render settings | **closed 2026-08-04** by replacing the derivation with the constant 46°. |
| `cross` yields no mesh at stock defaults | **closed 2026-08-04.** Not an empty mesh — a 17.5 GB OOM SIGKILL before the exporter ran. D22's tail, proven by a cliff at exactly 360/19, the model's own facet angle: `min_angle` 19 and above completes in 0.2 s and is valid, 18.9 and below is killed. |
| **unguarded `Decompose()` on the finished solid** | **open, new 2026-08-04.** `FilletBuilder.cc:3400`. Materialises a full mesh per component, so any high component count turns an invalid-mesh bug into a machine-killer — `sample(1)` put 1572 of 1572 main-thread samples there. The `unionCells` comment at `:1421` already names the hazard ("cost 112 s and 17 GB on a plate of a hundred bosses"); the final call is that path, unguarded. Fixing the threshold stopped `cross` reaching it; the path is still unsafe. |
| **`rib_into_boss` invalid at `$fn`=14 and 32** | **open, new 2026-08-04.** χ=4/3 non-manifold and χ=3/2 non-manifold, weld 1e-6. Same corner as the fin, smaller fault, on the bead surface where the two beads cross. |
| D23 — size gate drops creases on impossible misses | diagnosed, unfixed. The "equal radius" framing is recorded as wrong. |
| D24 — bead truncated and left open at a refused neighbour | diagnosed, unfixed. All-planar repro. **But the bench reads `bnd=0` on all 35 tiles**, including tiles where the size gate refuses four chains, so it may be masked rather than live. Under investigation. |
| D19 — subtractive scalloped ledge | parked at tag `d19-wall-recognition` (`fe8c8d9d1`). |

**The corpus is blind to the family these belong to**: all 225 models write an explicit `$fn`, and
no pass has looked at a junction render. This session demonstrated the cost twice — the corpus
called the `arrivesStraight` branch clean and one rendered junction found a fin with odd Euler
characteristic inside it; and `box_step`'s recorded genus regressions turned out to be artefacts
of the same blind spot.

**The bench has the same blind spot one axis over.** `expect.txt` carries one `$fn` per model,
so a fault that appears at some tessellations and not others is invisible to it — which is
exactly how `rib_into_boss` at `$fn`=14 and 32 survives a green bench. A `$fn` axis is the
single highest-value addition to the instrument.

---

## 6. Branches and where records live

Only two worktrees exist on disk: the main checkout, and the render worktree. Everything from this
session is on `kerem-fillet`; nothing is stranded.

**Live:**

| branch | contents |
|---|---|
| `kerem-fillet` | **everything current.** 19 unpushed commits. |
| `worktree-agent-a1065db1b78cc5fd6` | render worktree. Its `fillet-feature-design/` content is a strict subset of `kerem-fillet`'s; source is merely older. Carries nothing unique — safe to delete. |

**Record archive** — read with `git show <branch>:<path>`, do not check out; these worktrees were
deleted:

| record | command |
|---|---|
| integration, 805 lines | `git show integ-three:work/INTEGRATION.md` |
| D20 | `git show worktree-agent-a1cc15393bdb9006e:work/NOTES.md` |
| D21 | `git show worktree-agent-ad442fb5e2bfb0c44:work/D21.md` |
| D17.3 | `git show fix-blockers:work/BLOCKERS-AB.md` |
| D19 | `git show worktree-agent-a60c66ea1a5277bb6:work/D19.md` |
| `Chain::verts` audit + patches | `git show audit-chain-verts:work/CHAIN-VERTS-AUDIT.md` |

Also present: `integ-all-four` (superseded integration attempt), and a number of
`worktree-agent-*` branches sitting at `944e0cbef`, `489946544` or `faf6e1762` that were never
advanced.

**This session's documents** are all on `kerem-fillet` and all now under `archive/`, except
`decisions/2026-08-04-arrives-straight-stays.md`, which stays at root: the measurements,
controls, reviews, and the `arrives-straight-images/` PNGs.

The whole of `fillet-feature-design/` is a dev artifact and is deleted before merge. Archiving
is about what the next reader has to open, not about preservation — git holds it either way.

---

## 8. PR review items still open

Lifted from `archive/pr-review.md` when it was archived. R3 and R4 are closed; R1, R2 and R5
were re-verified against the tree as still present on 2026-08-04.

**Blocking:**

**None of these are implemented yet.** Commit `39557c022`, "Ship the fillet modules behind an
experimental flag, and drop them without Manifold", touched only `ACCEPTANCE.md` and
`STATE.md` — it wrote the criterion, not the code. `Feature::ExperimentalFillet` does not
exist in `src/Feature.h` or `src/Feature.cc`, and `register_builtin_fillet` still registers
all five modules unconditionally. A commit message in the imperative reads as done; check the
diffstat.

- **R1** — `buildFilletTool` echoes a mesh-statistics line unconditionally
  (`FilletBuilder.cc:3498`), once per tool node and twice per `fillet()`. No other OpenSCAD
  operator prints on success. Gate it behind `debug=`; the conditional warnings below it stay.
- **R2** — on a build without Manifold, `fillet()` **deletes the model**.
  `GeometryEvaluator.cc:1065`, the `#else` branch, warns and leaves `geom` null. **Resolved
  differently from the review's proposal, on the owner's decision:** do not pass the child
  through — do not register the modules at all without Manifold, so calling one is an unknown-
  module error naming the line. `tests/CMakeLists.txt:1563` lists the cgal disables for the four
  `*-tool-tests` but not for `fillet-tests`, which needs either the three disables or a baseline
  holding in both configurations.
- **Experimental gating** — new, and not from the review. The five modules must register behind
  `Feature::ExperimentalFillet` the way `roof` does (`RoofNode.cc:62`); today all five in
  `register_builtin_fillet` (`FilletNode.cc:205`) pass no feature pointer and are
  unconditionally available. Every regression test that invokes them then needs
  `--enable=fillet`. Written up under Availability in `ACCEPTANCE.md`.
- **R5** — `TEST_CASE("zzdebug rib", "[.]")` at `FilletBuilder_test.cc:1577` is a scratch test
  hidden behind a Catch2 tag, and it ships. Delete it.
- **R7** — the comment register. Decided, not open: between a third and two fifths of
  `FilletBuilder.cc` is prose in a voice the tree does not use, and it does not ship in that
  form. Keep, at a line or two each — the value of a non-obvious constant and why it is that
  value; the failure a construction exists to avoid, stated as fact; an invariant a caller must
  not break; a genuine surprise in the geometry or in Manifold. Cut rhetorical framing, the
  narrative of alternatives tried, restatements of the code, and second-person address. Target
  roughly a third of current volume, same pass over `FilletBuilder_internal.h`, `FilletNode.cc`
  and the two test files. Landed commit messages are history and are not rewritten.

**Worth doing:** R6 — `FilletBuilder.cc` is now **3777 lines**, up from the 2518 the review
complained about; `FilletBuilder_internal.h` already names the six separable pieces. R8 — the
warnings are essays; OpenSCAD warnings are one line, so keep the first sentence and the
coordinates. R9 — `classifyEdge`'s `aFar` seeding makes a degenerate triangle answer "convex";
`unionCells` skips `dropVolumelessParts` only on the single-cell path; `epsAt` reads
`endSections[j.vert]` through `map::operator[]` on a read path; `FilletNode.cc` is out of
alphabetical order in `CORE_SOURCES`.

**Documentation defect, found 2026-08-04:** `fillet-pr/doc-page/fillet.md:291` says the tools
"warn and emit nothing" under the CGAL backend. That is wrong. The fillet path never reads
`RenderSettings::backend3D`; it is gated only on the compile-time `ENABLE_MANIFOLD`, the cgal
test disables sit under `if(NOT ENABLE_MANIFOLD)`, and
`tests/regression/render-cgal/round-tool-tests-expected.png` is a passing 19 KB render of real
filleted geometry. `--backend=cgal` is supported and tested. The true limitation is a build
without Manifold — which is R2.

---

## 7. Corrections to the earlier record, and broken instruments

Recorded here because each was believed and each was false.

- `handoff-2026-08-03.md:274` — "all 13 controls byte-identical either way" is **false**; four move
  (`pocket`, `rhomb`, `slab`, `corner`). Likely carried from a D17.3-in-isolation control, never
  re-checked on the integrated tree.
- The list of **15** nondeterministic corpus models omits `box_L_fn96_r1` and `box_T_fn96_r1`. Since
  the first was counted as a real mover, the recorded "5 of 210" is one high. The raw "17 of 225" is
  correct and not understated — 13 of the 17 are vertex-order noise.
- The "13 controls" are **12 distinct models**: `boss.scad` and `ctrl1.scad` are byte-identical.
- There are **two** `ScopedSizeGateRule` users, not three, and one of them *is* the scale-invariance
  test — both were guard-strips, neither a deletion.
- `kSeamOverMax`'s replacement comment claimed a ceiling of "about twice" 0.10·r. The sweep supports a
  **floor** at 0.08 and no upper edge short of 2.0; 2.0 was the top of the swept range, not a measured
  failure boundary. Corrected in source.

**Instruments found broken (nine, cumulative):** `-o /dev/null` makes OpenSCAD skip the render and
report zero calls; `timeout(1)` does not exist on this machine and made an export loop report 19/19
FAILED; Catch2 splits test names on commas, so an unescaped test exclusion excludes nothing and
silently reports the full total. The standing rule — run any new metric on a case whose answer is
already known — earned its place again three times this session.

**Environmental:** ten agent runs were lost to the 10-minute stall watchdog, host process exit and a
network failure. Only committed work survived, every time.
