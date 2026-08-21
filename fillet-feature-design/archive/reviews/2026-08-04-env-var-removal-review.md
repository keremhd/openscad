# Adversarial review — the env-var removal (`06b301f2d`, recorded in `53874d2f4`)

Branch `kerem-fillet`, HEAD `53874d2f4`, working tree clean at the start and at
the end. Nothing in `src/` or `tests/` was edited by this review.

**Verdict: no code blocker. The change is a proven-faithful "flag on".**
Five findings, one of them a false statement of measured evidence written into
shipped source, and one that the pass's own shell-level evidence does not
measure what it was offered as measuring. Details in §3.

---

## 1. What was re-derived, and how

### 1.1 Build provenance

The binaries found on the branch had mtimes (`01:14:41`, `01:14:43`) three and
five seconds after the source mtime (`01:14:38`) — consistent with the record's
"restored from a saved copy", but not with a compile. **They were not trusted.**
`src/geometry/fillet/FilletBuilder.cc` and `FilletBuilder_test.cc` were touched
and both targets rebuilt from scratch source:

| binary | mtime | size |
|---|---|---|
| `build/OpenSCAD.app/Contents/MacOS/OpenSCAD` | 4 aug 01:23:40 | 24306808 |
| `build/OpenSCADUnitTests` | 4 aug 01:23:42 | 23624368 |

Both newer than the last source edit, both confirmed by `ls`, not by
"Built target". Target is `OpenSCADExe`. The compile produced no warnings from
`FilletBuilder.cc` (only the pre-existing `ld` macOS-version warnings).
Every measurement below is on these two binaries.

### 1.2 The equivalence proof — site by site

`git show 06b301f2d -U0 -- src/geometry/fillet/FilletBuilder.cc` yields
**exactly 16 hunks**, all enumerated:

| hunk (old line) | change | equivalent to old flag-on path? |
|---|---|---|
| `341` | drop `size_t redivided = 0;` | yes — counter read only by the deleted printout |
| `425` | drop `++redivided` + `RESAMPLE_DEBUG` block | yes — no control flow touched (checked at `-U18`; the `for` body and its brace survive intact) |
| `1027` | drop `sizeGateRuleOverride`, `filletSizeGateAsksBlindCreases`, `ScopedSizeGateRule` ctor/dtor, `filletGateDiagnostics` | yes — all four were env/test plumbing |
| `1291` | `filletSizeGateAsksBlindCreases() && A` → `A` | yes — the function returned `true` with the var unset and no override |
| `1296` | drop `FILLETGATE` printout | yes — stdout only |
| `2770` | drop `const bool localGroup = getenv(...)`, reword comment | yes |
| `2783` | `if (localGroup) { … }` around the `arrivesBent` fill → unconditional | yes |
| `2795` | drop `if (!localGroup) return false;` in `seamVertex` | yes |
| `2804` | `kSeamOverDefault`/`kSeamOverMax` → `constexpr double seamOver = 0.10` | yes (see finding F1 for the comment) |
| `2813` | drop the `seamOver` lambda, the `SEAMOVER` parse and its `LOG` warning | yes — the lambda returned `kSeamOverDefault` with the var unset |
| `3132` | comment only | — |
| `3151` | `if (seamOver > 0.0 && seamVertex(vert))` → `if (seamVertex(vert))` | yes — `0.10 > 0.0` |
| `3227` | comment only | — |
| `3252` | comment only | — |
| `3285` | drop `localGroup &&` from `servedEnd(i)` | yes |
| `3293` | drop `localGroup &&` from `seamVertex(j.vert)` | yes |

Three unconditional `seamVertex()` call sites existed *before* the change
(old `:2850`, `:3279`, `:3368`); they returned `false` through the deleted early
return, so they too are covered by the same fold. `seamOver` has exactly two
readers in the old code (`seamOver > 0.0` and `want = seamOver * r`) and one in
the new (`want = seamOver * r`) — **the constant is genuinely `0.10 * r`
everywhere it was read.**

There is **no site at which the shipped code differs from the old
`LOCALGROUP=1`, `SEAMOVER` unset, `SIZEGATE` unset path.** No mis-transcription.
This is a stronger result than a 13-model byte A/B, because it holds for every
model, not only the ones measured.

### 1.3 The 13 controls, re-derived from two independent directions

Post-removal column, re-exported with the binary of §1.1, all five variables
explicitly unset, runner in `bash`:

| model | md5 / bytes (mine) | implementer's post column |
|---|---|---|
| ctrl1 | 32022214 42229 | same |
| ctrl2 | f51c531d 1165339 | same |
| ctrl3 | c2abb1cb 1218310 | same |
| ctrl4 | 2739fd21 702470 | same |
| ctrl5 | ae43dfd5 1764712 | same |
| boss | 32022214 42229 | same |
| grid | c5f36622 1491318 | same |
| rib | 4af898e7 2655295 | same |
| pocket | fc13e94c 44098 | same |
| rhomb | b723db2f 40895 | same |
| slab | b0d8059c 40026 | same |
| corner | c3d3a98c 39717 | same |
| grid100 | 7244484e 4140160 | same |

Instrument validated on a known answer before use: `rib` came out `4af898e7…` /
`2655295`, the value recorded in `controls/2026-08-04-sizegate-13-13-control.md`
before this change existed.

The pre-removal (flag-off) column was **not** taken by rebuilding. It was read
out of git, from the integration pass's own artifacts
`integ-three:work/ab/c_f_<m>.stl` — which `controls.sh` produced with
`-u OPENSCAD_FILLET_LOCALGROUP -u OPENSCAD_FILLET_SEAMOVER`, i.e. flag off.
All thirteen md5/size pairs match the implementer's pre-removal column exactly,
including `pocket 4682fb6c/279059`, `rhomb 6c8475f6/273068`,
`slab fbb073df/278834`, `corner d854f3ec/132288`.

**Confirmed: 9 of 13 byte-identical, 4 differ — pocket, rhomb, slab, corner.**
The record's `handoff-2026-08-03.md:274` claim of "all 13 controls byte-identical
either way" is false, as the implementer says.

Soundness of the four, read from my own run: `pocket` NoError genus 0 80v/156f,
`rhomb` NoError genus 0 72v/140f, `slab` NoError genus 0 72v/140f, `corner`
NoError genus 0 84v/164f — all matching the implementer's numbers, and all
sound. `slab.scad` and `corner.scad` are indeed the two brush unit tests'
models, literally (`cube([10,10,10])`, `r = 2`, `slab = 0.5`; `r = 1`,
`h = 0.5`), and the slab test's comment names the two candidate shapes it is
deciding between — "the hull of four vertical cylinders … and not the hull of
eight spheres" — so the tests are load-bearing for the rule, not incidental.

### 1.4 `seamRoom` and the retreat fallback — measured, not read

Claim 6 was not taken on the code's word. The record's Blocker-A sweep was
re-run on the shipped default, no environment variables at all, on
`integ-three:work/ab/opocket2.scad` at `TH=60 RR=3 PROUD=1`, volume computed by
a divergence-theorem parser validated first on `cube([2,3,5])` → `30.000000`:

| TW | 5 | 1.55 | 1.5 | 1.4 | 1.0 | 0.15 |
|---|---|---|---|---|---|---|
| proud volume, shipped default | 0 | 0 | 0 | 0 | **0.011858** | **2.430507** |
| recorded "with them" column | 0 | 0 | 0 | 0 | 0.011858 | 2.430507 |

Exact reproduction to six decimals. The overrun, `seamRoom`, the room-halving
and the retreat fallback (`FilletBuilder.cc:3078-3089`) are live on the shipped
path and produce the numbers the record says they do. A second detector,
`oblq.scad` in `MODE = "proud"`, is empty at TW ≥ 1.5 and non-empty at
TW ≤ 1.0 — the same boundary, and a demonstration that the detector fires.

### 1.5 Soundness outside the 13

Sixteen further models from `integ-three:work/ab` were exported on the shipped
default: `bars brush clip corner_ideal ctrl_boss gap multi opocket opocket2
p_run pocket_ideal ribfa slab_ideal toolonly` — **all `Status: NoError`,
genus 0.** (`cmp2` is a compare harness needing `a.stl`/`b.stl`; `oblq` is the
proud detector of §1.4 and is empty by design.) This is not the 225-model
corpus, but it is fourteen models beyond the controls with no unsound result.

### 1.6 Suite and shell-level tests

* All five variables unset: **All tests passed (1709 assertions in 85 test
  cases)** — the prediction, met.
* All five set at once (`LOCALGROUP=1 SEAMOVER=0.3 SIZEGATE=legacy
  SIZEGATE_DEBUG=1 RESAMPLE_DEBUG=1`, one `env` invocation): **1709 / 85.**
  Hermetic.
* Per case: *"size: a crease the exemptions cover is judged the same at any
  scale"* → **13 assertions in 1 test case, passed**; *"…entirely is still asked
  about"* → **8**; *"brush: a corner one crease is cut short of gets no corner
  cell"* → **38**; *"brush: a slab over the top face …"* → **73**.
* `ctest -R fillet` → **21 of 21 passed**, 11.8 s, against
  `build/OpenSCAD.app/Contents/MacOS/OpenSCAD` (checked in
  `build/tests/CTestTestfile.cmake`, so the right binary). But see F5.

The scale-invariance test is present, not skipped, not disabled, and not
trivially passing: it asserts six `OffFace` verdicts at 1e-6 scale, checks each
scaled amount against the unit-scale amount times the scale, and then pins the
*shape* of the answer against two hand-derived constants,
`66.4313 * k` and `121.0920 * k`. Its 13 assertions are 5 `REQUIRE`/`CHECK`
plus a 6-iteration loop plus 2 pins.

### 1.7 Symbols and references

`strings -a` on both freshly built binaries: **0** `OPENSCAD_FILLET` matches.
Repo-wide `grep` for `OPENSCAD_FILLET|FILLETGATE|FILLETRESAMPLE|
ScopedSizeGateRule|localGroup|SeamRule|kSeamOver`, excluding `.git`, `build/`
and `fillet-feature-design/`: **no matches.** No `getenv` in
`src/geometry/fillet/`. No `EXPERIMENTAL` in `src/geometry/fillet/`. No
`setenv`/`getenv` left in `FilletBuilder_test.cc`.
`FilletBuilder_internal.h` carries no stale declaration of the deleted class or
of either deleted free function.

---

## 2. Verdict on each of the six claims

| # | claim | status |
|---|---|---|
| 1 | 1709 / 85, unchanged | **confirmed** (all-unset and all-five-set; the other five single-variable environments were not re-run — they cannot differ, nothing reads them) |
| 2 | zero symbols, zero references, no `getenv` | **confirmed** on rebuilt binaries |
| 3 | 8 and 13; scale-invariance test survives and means something | **confirmed** |
| 4 | `SeamRule` deleted, brush tests at 38 and 73 | **confirmed** |
| 5 | 9 identical / 4 differ; post default == pre `LOCALGROUP=1`; not a regression | **confirmed**, and the equivalence strengthened from a 13-model byte check to a source-level proof over all 16 hunks (§1.2) |
| 6 | `arrivesStraight`, `seamRoom`, retreat fallback survive; `arrivesStraight` now unconditionally live | **confirmed**, and the retreat fallback measured, not merely read (§1.4) |

---

## 3. Findings

### F1 — a measured ceiling was restated ten times smaller than the constant it replaced. `FilletBuilder.cc:2749-2751`

The removed code carried `constexpr double kSeamOverMax = 2.0;` with the
comment *"the largest the override may ask for. The ceiling is the top of the
measured basin: past it the overrun is longer than the corner it is repairing
and starts writing over the beads either side of it."* The ceiling was
**2.0 of the radius**, twenty times the default.

The replacement comment moves that same sentence onto the default:

> *"Longer than about twice this the overrun is longer than the corner it is
> repairing and starts writing over the beads either side of it, so there is no
> room above to move into either."*

"about twice this" is **0.20 of the radius**. The number is off by a factor of
ten from the constant it is derived from, and no measurement on this branch
supports it either: the sweep the removal rests on
(`removal-scope.md`, from `BLOCKERS-AB.md:214-230`) gives 0.05/0.15/0.2/0.3 →
10/12/13/12 bad rows against the default's 9, i.e. the overrun is already worse
at **0.15**, and the basin has no visible top at 0.20. The clause "so there is
no room above to move into either" is then used to justify not raising the
constant — an argument resting on a number that is neither the removed
constant nor the sweep.

This is a comment, so it changes no behaviour. It is nonetheless a false
statement of measured evidence introduced into shipped source, in the one place
a future reader will look to find out why the constant is 0.10. **Fix by
restating the removed constant's own figure (a ceiling of 2.0·r, top of the
measured basin) or by dropping the ceiling clause entirely and leaving the
floor argument, which is the part the sweep supports.**

### F2 — dead variable left by the diagnostic removal. `FilletBuilder.cc:1171, 1193`

`double offTested = 0.0;` and `offTested = std::max(offTested, c.offFace);` are
all that is left of it: its only reader was the `FILLETGATE` printout deleted at
old `:1296-1306`. It is now written and never read. No compiler warns, because
the assignment also reads it. `removal-scope.md` S1 caught the identical case
for `redivided` and the implementer folded that one away; this one was missed.

### F3 — four includes with no remaining user. `FilletBuilder.cc:27, 28, 31, 35`

`<cstdio>` (only `fputs`), `<cstdlib>` (only `getenv`), `<locale>` (only
`imbue`) and `<sstream>` (only `ostringstream`/`istringstream`) have no user
left anywhere in the file.

### F4 — two comments still describe the knob. `FilletBuilder.cc:2759-2760` and `:3180-3186`

* `:2759` *"Not gated on the overrun: the vertex gets no ball whatever the
  overrun is"* — the overrun is now a constant that cannot be zero, so there is
  no gating question to disclaim.
* `:3182` *"and one flag for the whole call gives the served corner's answer to
  every withheld one"* — "one flag for the whole call" is
  `OPENSCAD_FILLET_LOCALGROUP`, which no longer exists. The paragraph's
  reasoning survives; its subject does not.

The pass explicitly reworded the `EXPERIMENTAL (env …)` heads and struck the
`legacy` sentences; these two are the ones it did not reach.

### F5 — `ctest -R fillet` 21/21 is not evidence that the flag flip is safe

`removal-scope.md` §5 added the four `fillet-tests` / `fillet-tool-tests`
baselines to the must-re-run list *because `LOCALGROUP` flips on*, and
`env-var-removal.md` §6 reports the green run as *"No image or dump baseline
needed re-pinning."* That reads as coverage. It is not:

* `dump_fillet-tests` / `dump_fillet-tool-tests` compare
  `tests/regression/dump/fillet-tests-expected.csg` (917 bytes) and
  `fillet-tool-tests-expected.csg` (1431 bytes). A `.csg` dump is the scene
  graph. **It contains no mesh at all**, so it cannot move when the geometry
  does.
* The other sixteen are `render-*`, `preview-*` and `throwntogether-*` image
  comparisons with a pixel tolerance.
* The baselines were pinned with the rule off (nothing in `tests/regression/`
  has been re-pinned on this branch) and still pass with it on — which means
  the models in those two `.scad` files are unaffected by the flip.

The correct reading is therefore the opposite of the one recorded: **the
shell-level regression suite is blind to the seam rule**, and after the
`fillet-feature-design/` tree is deleted before merge (owner standard 9),
`pocket/rhomb/slab/corner` go with it. The rule's only surviving guard will be
the two C++ brush tests. Those two do look load-bearing — the slab test names
the two shapes it is choosing between, and the flag-off mesh is 976 triangles
at genus −1 against the flag-on 140 at genus 0, which 73 assertions could not
miss — but that is an inference, not a measurement (§4).

Not a blocker on this change; a gap the merge should close, by promoting one of
the four differing controls into `tests/data/scad/3D/features/` with a real
baseline.

### F6 — the control set is 12 models, not 13

`boss.scad` and `ctrl1.scad` are the same model: a 40×40×4 plate with a
16×16×12 boss, `$fn = 64`, `r = 2` — `boss.scad`'s header comment describes a
cylindrical boss it does not contain. They export byte-identical STL
(`32022214` / `42229`) on every build, in the pre-removal control and in mine.
Pre-existing, not caused by this change, but it means "9 of 13 identical" is
"8 of 12 distinct, one counted twice", and every 13/13 and 12/13 figure in the
record has the same duplicate in it.

---

## 4. What this review did **not** check

* **The 225-model corpus.** Not run. The implementer names it as their own
  biggest gap and it stays open: with `handoff-2026-08-03.md:274`'s
  byte-identity claim now falsified, the "17 of 225 differ, all sound" figure
  from the same pass is unverified and may understate the count. §1.5's
  fourteen extra sound models are a sample, not a substitute.
* **The 144-run box census and the 81-shape D21 family** — another agent holds
  those.
* **Whether the two brush tests actually fail with the rule off.** Proving it
  needs a mutated build of `OpenSCADLibInternal`, which would put a mutated
  library under a concurrently measuring agent. Deliberately not done.
* **Timing.** The now-unconditional `arrivesStraight` pass over every open chain
  end was not measured, on any model.
* **`$fn` and scale sweeps**, and any CGAL-backend geometry beyond what
  `ctest -R fillet` covers.
* **Byte-identity against a clean `944e0cbef` build.** No such binary was built;
  every comparison here is pre-removal-vs-post-removal on `kerem-fillet`.
* **The five single-variable environments** of the record's seven — only
  all-unset and all-five-set were re-run.
* **`SEAT` / `WALLFACE`** beyond confirming no reader exists.
* The open owner question from `removal-scope.md` §6 — whether
  `arrivesStraight` should ship at all — is untouched. Shipping it on is what
  this pass does, and no measurement on this branch has ever been taken with it
  removed from the merged tree.
