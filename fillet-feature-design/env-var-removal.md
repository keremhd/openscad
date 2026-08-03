# Env-var removal — the pass, and everything measured

Branch `kerem-fillet`. Code commit **`06b301f2d`** ("Ship the fillet seam rule
and size gate on, with no environment variables"), parent `529c5437b`.

Scope followed verbatim from `fillet-feature-design/removal-scope.md`
(commit `33b5baade`). Governing directive: every mechanism ships on
unconditionally or does not ship. No knobs.

---

## 1. What changed

`git diff 529c5437b..06b301f2d --stat`:

```
 src/geometry/fillet/FilletBuilder.cc         | 147 +++-------------------
 src/geometry/fillet/FilletBuilder_internal.h |  19 ----
 src/geometry/fillet/FilletBuilder_test.cc    |  32 ------
 3 files changed, 31 insertions(+), 167 deletions(-)
```

All 17 source sites and 6 test sites in the scope, applied as inventoried:

| var | shipped as | sites |
|---|---|---|
| `OPENSCAD_FILLET_LOCALGROUP` | **ON unconditionally** | S9–S12, S14–S17 |
| `OPENSCAD_FILLET_SEAMOVER` | **constant `0.10` of r** | S12–S13; `kSeamOverMax` and the locale-safe parse and its `LOG` warning went with it |
| `OPENSCAD_FILLET_SIZEGATE` | **ON unconditionally** (`asksBlindCreases == true`) | S2–S5, S8 |
| `OPENSCAD_FILLET_SIZEGATE_DEBUG` | **does not ship** | S6, S7 |
| `OPENSCAD_FILLET_RESAMPLE_DEBUG` | **does not ship** | S1; `redivided` counter folded away |
| `SEAT` / `WALLFACE` | no reader existed | 0 sites, confirmed |

Deliberately **not** touched, per the brief:

* `seamRoom`, the room-halving and the **retreat fallback** at the old
  `:3169-3174` — all present and live. The earlier review's "delete three
  mechanisms" conclusion is recorded as wrong and was not acted on.
* `arrivesStraight` — now always consulted, which is the correct mechanical
  result of shipping `LOCALGROUP` on. Left live and untouched.

The doc comments that justified each mechanism were kept and reworded; only the
`EXPERIMENTAL (env …)` framing and the `legacy`/knob sentences were struck.
`grep -rn EXPERIMENTAL src/geometry/fillet/` → none.

### Tests

* `struct SeamRule` (`:51-73`) — **deleted outright**. Its two uses at `:1287`
  and `:1371` were line-strips; **both tests and every pin stay.**
* `ScopedSizeGateRule` guards at `:1996` and `:2035` — **guard-strip only.**
  Both tests kept. The `OPENSCAD_FILLET_SIZEGATE=legacy` sentence in the first
  test's comment was reworded away.
* `ScopedSizeGateRule` class (`FilletBuilder_internal.h:449-467`) — deleted, now
  unused.

---

## 2. Grep and symbol verification

```
grep -rn "OPENSCAD_FILLET" src/ tests/                       -> (none)
grep -rn "localGroup|SeamRule|ScopedSizeGateRule|kSeamOver|
          filletGateDiagnostics|filletSizeGateAsksBlindCreases|
          sizeGateRuleOverride|redivided|FILLETGATE|FILLETRESAMPLE" src/ tests/  -> (none)
grep -rn "getenv" src/geometry/fillet/                       -> (none)
strings -a build/OpenSCAD.app/Contents/MacOS/OpenSCAD | grep -c OPENSCAD_FILLET  -> 0
strings -a build/OpenSCADUnitTests                   | grep -c OPENSCAD_FILLET  -> 0
```

The pre-removal binary yields **6** such symbols, the post-removal one **0** —
so the count is discriminating, not vacuous.

---

## 3. Build and mtime evidence

Target is `OpenSCADExe`, not `OpenSCAD`. Binaries confirmed to exist and to have
changed size by `ls`, not by trusting "Built target".

| stage | source last edit | binary | mtime | size |
|---|---|---|---|---|
| pre-removal | 4 aug 00:16:15 | `build/OpenSCAD.app/.../OpenSCAD` | 4 aug 00:24:11 | 24308792 |
| pre-removal | — | `build/OpenSCADUnitTests` | 4 aug 00:24:16 | — |
| post-removal | 4 aug 01:03:29 (`FilletBuilder.cc`) | `build/OpenSCAD.app/.../OpenSCAD` | 4 aug 01:03:51 | 24306808 |
| post-removal | — | `build/OpenSCADUnitTests` | 4 aug 01:03:57 | 23624368 |
| pre-removal, rebuilt for the A/B | — | same path, saved to `scratchpad/bins/pre_OpenSCAD` | 4 aug 01:12:47 | 24308792 |
| post-removal, restored | — | `build/OpenSCAD.app/.../OpenSCAD` | 4 aug 01:14:41 | 24306808 |

Every binary mtime is newer than the last source edit preceding it. The size
change (24308792 → 24306808) and the symbol count (6 → 0) both confirm the new
source is actually in the binary.

---

## 4. Suite — prediction stated before measuring

**Predicted: 1709 assertions in 85 test cases, unchanged.** Nothing deleted
carries an assertion; `SeamRule` is a fixture and the two guard-strips lose a
declaration line each.

Baseline measured on the pre-removal binary: **1709 assertions in 85 test
cases**, green.

Post-removal, seven environments:

| environment | result |
|---|---|
| all five UNSET | All tests passed (**1709 assertions in 85 test cases**) |
| `OPENSCAD_FILLET_LOCALGROUP=1` | All tests passed (**1709 / 85**) |
| `OPENSCAD_FILLET_SEAMOVER=0.3` | All tests passed (**1709 / 85**) |
| `OPENSCAD_FILLET_SIZEGATE=legacy` | All tests passed (**1709 / 85**) |
| `OPENSCAD_FILLET_SIZEGATE_DEBUG=1` | All tests passed (**1709 / 85**) |
| `OPENSCAD_FILLET_RESAMPLE_DEBUG=1` | All tests passed (**1709 / 85**) |
| all five set at once | All tests passed (**1709 / 85**) |

**Prediction met exactly. The number did not move; no pin was edited.**
The vars are inert, and the suite is hermetic against every one of them
including a stale `SIZEGATE=legacy`.

### The four tests that lost a guard, by name

| test | assertions | result |
|---|---|---|
| `size: a crease the exemptions cover entirely is still asked about` | **8** | passed |
| `size: a crease the exemptions cover is judged the same at any scale` | **13** | passed |
| both together under ambient `OPENSCAD_FILLET_SIZEGATE=legacy` | 21 in 2 cases | passed |
| `brush: a corner one crease is cut short of gets no corner cell` | **38** | passed |
| `brush: a slab over the top face rounds its edges and leaves the corners square` | **73** | passed |

The 8 and 13 are exactly the per-case contributions the scope's §4 arithmetic
predicted from `D21.md:222-225` and `INTEGRATION.md:528`. **The scale-invariance
test survived the removal**, which was the brief's explicit hazard.

The two brush tests passing *without* the `SeamRule` fixture is the direct
positive evidence that the seam rule is now on by default: previously they only
passed because the fixture exported `LOCALGROUP=1` for their bodies.

---

## 5. The 13-model control, re-run

Models extracted read-only via `git show integ-three:work/ab/<m>.scad`; no
worktree checked out. Runner is **bash**, not zsh (`env $VAR …` in zsh performs
no word splitting). Every row checkpointed to a file as produced. All five
variables explicitly unset on every run.

Instrument validated against a known answer before use: the pre-removal `rib`
md5 came out `4af898e7…`, which is the exact prefix recorded in
`controls/2026-08-04-sizegate-13-13-control.md`. The volume parser likewise
reproduced the recorded `rib` volume **21105.659735** to all six decimals.

### Result — post-removal default vs pre-removal default

| model | pre-removal default | post-removal default | verdict |
|---|---|---|---|
| ctrl1 | 32022214… 42229 | 32022214… 42229 | identical |
| ctrl2 | f51c531d… 1165339 | f51c531d… 1165339 | identical |
| ctrl3 | c2abb1cb… 1218310 | c2abb1cb… 1218310 | identical |
| ctrl4 | 2739fd21… 702470 | 2739fd21… 702470 | identical |
| ctrl5 | ae43dfd5… 1764712 | ae43dfd5… 1764712 | identical |
| boss | 32022214… 42229 | 32022214… 42229 | identical |
| grid | c5f36622… 1491318 | c5f36622… 1491318 | identical |
| rib | 4af898e7… 2655295 | 4af898e7… 2655295 | identical |
| **pocket** | 4682fb6c… 279059 | **fc13e94c… 44098** | **DIFFERS** |
| **rhomb** | 6c8475f6… 273068 | **b723db2f… 40895** | **DIFFERS** |
| **slab** | fbb073df… 278834 | **b0d8059c… 40026** | **DIFFERS** |
| **corner** | d854f3ec… 132288 | **c3d3a98c… 39717** | **DIFFERS** |
| grid100 | 7244484e… 4140160 | 7244484e… 4140160 | identical |

**9 of 13 byte-identical; 4 differ.** Re-run end to end a second time: all 13
rows, md5s and sizes identical between passes, so none of these four is in the
15-of-225 nondeterministic class. The difference is real.

### FINDING — the record's "byte-identical either way" claim is false

`removal-scope.md` quotes `handoff-2026-08-03.md:274`: *"withheld models and all
13 controls are byte-identical either way."* **That is not true of these four.**
Four of the thirteen controls change when `LOCALGROUP` flips on.

I did not accept this on the record's authority and did not edit anything to
make it agree. I rebuilt the pre-removal binary and ran the decisive A/B:

**Post-removal default == pre-removal `OPENSCAD_FILLET_LOCALGROUP=1`, all 13
models, byte for byte, md5 and size.** MATCH on every row including all four
that differ.

So the change is a faithful "flag on" and introduces nothing of its own. The
four models move because the seam rule genuinely changes them — the record's
byte-identity claim was simply wrong, most likely carried over from the
D17.3-in-isolation control rather than re-checked on the integrated tree.

### The four are not a regression — two are repairs

| model | flag OFF (pre default) | flag ON (post default) |
|---|---|---|
| pocket | NoError, genus 0, 497 v / 990 t, vol 252352.600024 | NoError, genus 0, 80 v / 156 t, vol 252367.745216 |
| rhomb | NoError, **genus −1**, 492 v / 976 t, vol 6847.399976 | NoError, **genus 0**, 72 v / 140 t, vol 6832.254784 |
| slab | NoError, **genus −1**, 492 v / 976 t, vol 983.722236 | NoError, **genus 0**, 72 v / 140 t, vol 968.577043 |
| corner | NoError, genus 0, 237 v / 470 t, vol 998.417813 | NoError, genus 0, 84 v / 164 t, vol 997.945264 |

* All four are sound closed solids after the change (`Status: NoError`).
* **rhomb and slab go from genus −1 to genus 0** — the flag-on result is
  *sounder*, not worse. Genus −1 on a simple prism is a defect.
* Volumes move by 0.006 % (pocket), 0.05 % (corner), 0.22 % (rhomb) and 1.5 %
  (slab). The shape is essentially the same.
* The triangle collapse (3–6×) is the rule's designed effect, not a lost blend:
  a seam vertex builds **no corner ball**, and at `$fn = 64`/`96` the four
  corner balls are most of the triangle budget. The beads meet in a seam
  instead.
* `slab.scad` and `corner.scad` are *exactly* the models the unit tests
  `brush: a slab over the top face rounds its edges and leaves the corners
  square` and `brush: a corner one crease is cut short of gets no corner cell`
  pin. Both tests pass post-removal with all 73 and 38 assertions intact. **The
  post-removal shape of these two models is the pinned, intended one.**

### Did the feature fire?

Counted from the user-visible stream, since the `FILLETGATE` diagnostic is one
of the things removed:

| model | creases selected | brush lines | refusal warnings |
|---|---|---|---|
| ctrl1 4 · ctrl2 112 · ctrl3 12 · ctrl4 64 · ctrl5 128 · boss 4 | | 0 | 0 |
| grid | 144 | 1 | 0 |
| **rib** | 135 | 0 | **1** — *"does not fit 2 of the 3 creases"* |
| pocket 8 · rhomb 12 · slab 12 · corner 12 | | 1 each | 0 |
| grid100 | 400 | 1 | 0 |

**One refusal across the 13, on `rib`, dropping 2 of 3 creases** — the same
single blind refusal the pre-removal control recorded (174 chains / 172 served /
1 blind / 1 blind refusal, all on `rib`). The size gate is live, and `rib` is
byte-identical before and after, so the gate's behaviour is unchanged by the
removal. This is not byte-identical inertness: four models moved, and they moved
to the shapes the unit tests pin.

---

## 6. Shell-level regression tests

`ctest -R fillet` — **21 of 21 passed**, 11.69 s. This covers
`dump/render-cgal/render-manifold/preview-cgal/preview-manifold/throwntogether-cgal/throwntogether-manifold/render-csg-cgal`
for both `fillet-tests` and `fillet-tool-tests`, plus the five named fillet
cases. **No image or dump baseline needed re-pinning**, which is what the scope
§5 predicted ("confirm, do not pre-emptively re-pin").

---

## 7. What was NOT measured

* **The 225-model corpus.** The scope records 17 of 225 models moving when
  `LOCALGROUP` flips on, all outcomes sound. I re-ran only the 13 controls, and
  found 4 of those 13 move — so the "17 of 225" figure is not confirmed here and
  may itself be understated. **This is the single biggest gap.**
* **The 144-run box census** and the **81-shape D21 family** — neither re-run.
* **The `$fn` and scale sweeps** — not re-run.
* **`arrivesStraight` removed** — still not measured on the merged tree. The
  scope's §6 ambiguity is untouched and remains open for the owner. Shipping it
  on is what this pass does, which is the configuration the third-pass corpus was
  validated under.
* **No clean-`944e0cbef` build.** My byte-identity baseline is the pre-removal
  `kerem-fillet` tree, not clean HEAD — which is the right comparison for "does
  removal change the default", but it means the 12/13-vs-clean-HEAD figure from
  the earlier control was not re-derived.
* **Gate chain/served counts post-removal** were not measured directly, because
  the `FILLETGATE` diagnostic is one of the things deleted. They are inferred
  from `rib` being byte-identical plus the refusal-warning count. The
  174/172/1/1 figures come from the pre-removal control, not from a fresh read.
* **No performance measurement.** Turning the seam rule on unconditionally adds
  an `arrivesStraight` pass over every chain; its cost was not timed.
* **`SEAT` / `WALLFACE`** — confirmed absent by grep only; D19 is parked and not
  built here.
