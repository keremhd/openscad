# Env-var removal scope — exact inventory

Branch `kerem-fillet`, taken 2026-08-04. Read-only survey; no source was changed.

Governing directive: `handoff-2026-08-03.md` §"OWNER DIRECTIVE — no env vars ship"
(line 35) — *"Each mechanism either ships on unconditionally, or it does not ship."*

Shipping decision per mechanism, and the record it comes from:

| var | mechanism | ships as | evidence |
|---|---|---|---|
| `OPENSCAD_FILLET_LOCALGROUP` | D17.3 seam rule (no ball at a seam vertex, per-chain grouping) | **ON unconditionally** (today's `=1` path) | `INTEGRATION.md` RECOMMENDATION: *"Land D17.3 with them, **on**, with its env vars removed."* Third pass: 7 unsound both ways, same list; census identical; 17 of 225 models differ and every outcome stays sound. `handoff-2026-08-03.md:274` "Env-gating notes": *"Every improved number is taken with `LOCALGROUP=1`, so making the rule default merely promotes them; withheld models and all 13 controls are byte-identical either way."* |
| `OPENSCAD_FILLET_SEAMOVER` | overrun length, fraction of r | **fixed constant `kSeamOverDefault = 0.10`** (today's unset value) | `handoff-2026-08-03.md:274`: *"`SEAMOVER` is **not a defensible knob**"* — 0.05/0.15/0.2/0.3 give 10/12/13/12 bad rows against the default's 9, non-monotone; `SEAMOVER=0` is bad at 10 tessellations, so it is a diagnostic, not an alternative. `BLOCKERS-AB.md:214-230`. |
| `OPENSCAD_FILLET_SIZEGATE` | blind-crease fallback in the size gate | **ON unconditionally** (`asksBlindCreases == true`, today's unset value); `legacy` disappears | `D21.md:123` "Default on"; `D21.md:554` suite green both ways; `INTEGRATION.md:250` treats `legacy` as the thing being removed. |
| `OPENSCAD_FILLET_SIZEGATE_DEBUG` | per-crease stdout tally | **does not ship** — pure diagnostic, prints nothing on the default path | `D21.md:125`. |
| `OPENSCAD_FILLET_RESAMPLE_DEBUG` | re-division count on stdout | **does not ship** — pure diagnostic; its own comment says *"it changes nothing either way"* | `FilletBuilder.cc:428-430`. |
| `SEAT`, `WALLFACE` | D19 wall recognition | **nothing to remove — no reader exists on this branch** | `INTEGRATION.md:610`: *"zero references to `OPENSCAD_FILLET_SEAT`, `OPENSCAD_FILLET_WALLFACE` or `fitWall` in the source."* Confirmed here: `grep -rn 'SEAT\|WALLFACE' src/ tests/` returns no reader. D19 is parked at `d19-wall-recognition` and does not land (`INTEGRATION.md` RECOMMENDATION). |

**Mechanisms that must NOT be dropped with the knob.** `handoff-2026-08-03.md`
§"`seamRoom` and the retreat fallback survive, on evidence": with an unbounded
overrun and retreat disabled, every wall under 1.55 mm stands proud
(0.001451 / 0.003578 / 0.010666 / 0.187270 / 4.170465 against 0/0/0/0/0.011858/2.430507).
The earlier review's "delete three mechanisms" conclusion is recorded there as
**wrong**. `seamRoom`, the room-halving and the retreat fallback all ship.

---

## 1. `src/` sites

All in `src/geometry/fillet/`. Line numbers are on `kerem-fillet` as surveyed.

| # | site | file:line | value when unset today | action | shipping behaviour | risk |
|---|---|---|---|---|---|---|
| S1 | `RESAMPLE_DEBUG` printout | `FilletBuilder.cc:428-436` (getenv at `:431`) | block not entered | delete the whole `if` and its 3-line comment | nothing printed; `redivided` becomes unused — fold the counter away or keep it as the loop's own | none — output-neutral by construction |
| S2 | `sizeGateRuleOverride` file-scope int | `FilletBuilder.cc:1036` | `-1` (env decides) | delete | — | none |
| S3 | `filletSizeGateAsksBlindCreases()` + its `fromEnv` lambda | `FilletBuilder.cc:1038-1045` (getenv at `:1041`) | `true` (`legacy` not set) | delete the function; the `legacy` doc paragraph at `:1027-1035` shrinks to the rule's own justification | fallback always on | none at the call site; the whole re-run cost is downstream (see §5) |
| S4 | the call site | `FilletBuilder.cc:1291` | condition's first term always `true` | drop the `filletSizeGateAsksBlindCreases() &&` term | unchanged from default | none |
| S5 | `ScopedSizeGateRule` ctor/dtor | `FilletBuilder.cc:1047-1052` | — | delete | — | breaks the two test guards — see §2 |
| S6 | `filletGateDiagnostics()` | `FilletBuilder.cc:1057-1061` (getenv at `:1059`) | `false` | delete the function | — | none |
| S7 | `FILLETGATE` diagnostic block | `FilletBuilder.cc:1296-1306` | block not entered | delete | nothing printed | **note**: `D21-MARGIN.md` and `D21.md:450` state their evidence was read from this stream. It is a dev-artifact dependency only; the design log dir is deleted before merge (owner standard 9). |
| S8 | `ScopedSizeGateRule` class + its 6-line comment | `FilletBuilder_internal.h:449-467` | — | delete both | — | header is internal; no other consumer |
| S9 | `const bool localGroup = getenv(...)` | `FilletBuilder.cc:2776` | `false` | delete the variable; the `EXPERIMENTAL (env …)` comment at `:2770-2775` keeps its body, loses its first line | rule always on | **the only behavioural flip in the pass** — 17 of 225 corpus models move, all sound |
| S10 | `if (localGroup)` around `arrivesBent` fill | `FilletBuilder.cc:2783-2790` | skipped (set stays empty) | unconditional | `arrivesStraight` always consulted | see the *ambiguity* note below |
| S11 | `if (!localGroup) return false;` in `seamVertex` | `FilletBuilder.cc:2795` | always returns `false` | delete the early return | seam vertices are recognised | — |
| S12 | `if (!localGroup) return 0.0;` in the `seamOver` lambda | `FilletBuilder.cc:2820` | returns `0.0` | delete; the whole lambda collapses to `constexpr double seamOver = kSeamOverDefault;` | overrun is 0.10·r always | — |
| S13 | `getenv("OPENSCAD_FILLET_SEAMOVER")` parse + locale-safe `istringstream` + `LOG` warning | `FilletBuilder.cc:2821-2836` | returns `kSeamOverDefault` | delete outright, including the warning string | 0.10·r | none; `kSeamOverMax` (`:2808`) becomes unused and goes with it |
| S14 | `if (seamOver > 0.0 && seamVertex(vert))` overrun branch | `FilletBuilder.cc:3151-3176` | never entered | keep; the `seamOver > 0.0` guard is now a constant-true and may be dropped, `seamVertex(vert)` stays | overrun, `seamRoom`, half-the-room rule and the retreat fallback all live | **keep the retreat fallback at `:3169-3174`** — the record says explicitly it survives on evidence |
| S15 | the `EXPERIMENTAL (env …)` comment heads | `FilletBuilder.cc:2770`, `:3132`, `:3227` | — | strike the `EXPERIMENTAL (env …)` framing, keep the reasoning | — | comment-only |
| S16 | `if (localGroup && servedEnd(i)) continue;` | `FilletBuilder.cc:3285` | always `continue`-free → one global group | drop the `localGroup &&` | per-chain grouping where a seam is served | — |
| S17 | `if (localGroup && seamVertex(j.vert)) continue;` | `FilletBuilder.cc:3293` | never skips | drop the `localGroup &&` | seam vertices link no chains | — |

**Count: 17 source sites** — 1 for `RESAMPLE_DEBUG`, 6 for `SIZEGATE` + `SIZEGATE_DEBUG`
(S2-S8), 8 for `LOCALGROUP` (S9-S12 partly, S14-S17), 2 for `SEAMOVER` (S12-S13).
Getenv calls to remove: **5**, at `FilletBuilder.cc:431, 1041, 1059, 2776, 2821`.
`SEAT` / `WALLFACE`: **0 sites**.

---

## 2. Test sites

All in `src/geometry/fillet/FilletBuilder_test.cc` (this project keeps the unit
tests beside the source; `tests/` holds only the shell-level regressions, §3).

| # | site | file:line | action | shipping behaviour | risk |
|---|---|---|---|---|---|
| T1 | `struct SeamRule` fixture + its 4-line comment | `:51-73` | **delete outright** | the rule is on for every test, as it is for every user | none — recorded green |
| T2 | `const SeamRule seamRule;` in *"brush: a corner one crease is cut short of gets no corner cell"* | `:1287` (case opens `:1262`) | strip the line, **keep the test and every pin** | pins the shipped shape | none |
| T3 | `const SeamRule seamRule;` in *"brush: a slab over the top face rounds its edges and leaves the corners square"* | `:1371` (case opens `:1349`) | strip the line, **keep the test and every pin** | pins the shipped shape | none |
| T4 | `const ScopedSizeGateRule askBlindCreases(true);` in *"size: a crease the exemptions cover entirely is still asked about"* | `:1996` (case opens `:1975`) | **strip the guard line only — do NOT delete the test** | asserts exactly the shipped behaviour (all six creases `OffFace`, 66–121 mm) | see the correction below |
| T5 | `const ScopedSizeGateRule askBlindCreases(true);` in *"size: a crease the exemptions cover is judged the same at any scale"* | `:2035` (case opens `:2013`) | **strip the guard line only — do NOT delete the test** | expectation derived from *similarity*, not from output | the handoff's explicit "must not take the test with it" |
| T6 | the `OPENSCAD_FILLET_SIZEGATE=legacy` sentence in T4's comment | `:1993-1995` | reword | — | comment-only |

### Correction to the brief's premise — no D21 test needs deleting

The brief (and `handoff-2026-08-03.md:60-62`, and `INTEGRATION.md:250`) says
*"D21's two hermetic tests … will not compile"* and implies both are deletions,
with the scale-invariance test as a separate third survivor. **There are only
two `ScopedSizeGateRule` users in the tree, and one of them IS the
scale-invariance test.** The full list of hermetic users is T4 and T5; there is
no third.

Neither is a deletion:

* the *only* compile dependency is the RAII declaration line itself — nothing
  else in either test names the class, the function or the variable;
* the guard argument is `true`, and `true` is exactly what
  `filletSizeGateAsksBlindCreases()` returns when the variable is unset today
  and what the code does unconditionally after removal. The guard exists solely
  so that an ambient `SIZEGATE=legacy` in a bisecting shell does not redden the
  suite (`D21.md:403-408`). Once the variable is gone there is no ambient value
  to defend against, and the assertions are unchanged in meaning and in value.

So the removal is a **guard-strip on both**, and the arithmetic in §4 follows
from that. If the owner nonetheless wants T4 deleted, §4 gives that number too.

---

## 3. Everything else that could break

| where | finding |
|---|---|
| `src/CMakeLists.txt`, `tests/CMakeLists.txt` | **no reference to any of the variables.** `tests/CMakeLists.txt:1572-1574` lists `render/preview/throwntogether-cgal_fillet-tool-tests` only as backend-dependent baselines; nothing env-related. |
| shell-level `fillet-tests` | models are `tests/data/scad/3D/features/fillet-tests.scad` and `fillet-tool-tests.scad`; baselines in `tests/regression/{dump,render,preview,throwntogether}/`. **Neither .scad reads or mentions an env var.** Their shapes are boss-on-plate and the L — both are among the 13 controls the record calls byte-identical with `LOCALGROUP` on and off, so re-pinning is *not expected*; re-run to confirm rather than to update. |
| CI | no workflow sets any of these variables. |
| docs / user-facing | none. The variables were never documented outside `fillet-feature-design/`. |
| `fillet-feature-design/**` | many references — `repros/rev-final/{thsweep,mixsweep,ribgate}.sh`, `repros/rev-d17/{ribsweep,sw15,bcsweep}.sh`, `reviews/D17-FINAL.md`, `reviews/D17-REVIEW.md`, `reviews/D21-REVIEW.md`, `reviews/D21-MARGIN.md`, `handoff-2026-08-02.md`, `handoff-2026-08-03.md`. **All dev artifacts, deleted before merge** (owner standard 9). They must NOT be edited to hide the variables — they are the record of why the variables went. |
| repro scripts becoming unrunnable | the sweep scripts above stop being able to A/B once the source change lands. `handoff-2026-08-03.md:274`: *"Run the 13/13 control once more before the gate goes — it can only be run while the gate exists."* This is a **sequencing constraint on the removal pass, not a code site.** |

---

## 4. Suite prediction, stated before measuring

Baseline as recorded: **1709 assertions in 85 test cases**
(`INTEGRATION.md:526`, `:685` — third pass on `integ-three`, green in all seven
environments).

Provenance of the two D21 cases, for the arithmetic:

* clean HEAD / D19 / D20 alone: 1688 / 83.
* `+ "size: a crease the exemptions cover entirely is still asked about"` →
  1696 / 84 (`D21.md:222-225`), i.e. that case carries **8 assertions**
  (1 `REQUIRE` on the chain count, 1 `CHECK` on the fault count, 6 in the
  amount loop).
* `+ "size: a crease the exemptions cover is judged the same at any scale"` →
  1709 / 85 (`INTEGRATION.md:528`), i.e. that case carries **13 assertions**.

**Prediction: 1709 assertions in 85 test cases — unchanged.**

Arithmetic: `1709 − 0 = 1709`, `85 − 0 = 85`. Nothing is deleted. T1 is a
fixture and carries no assertion; T2–T5 lose a declaration line each and keep
every `CHECK`/`REQUIRE`. The three environment-dependent behaviours all move to
the value the suite was already green under — `INTEGRATION.md:531` records that
**"not one assertion or case count moves with any environment"** across 35 runs,
which is the direct evidence that fixing the defaults cannot move the total.

Contingency, if the owner overrides §2 and deletes T4: **1701 / 84**
(`1709 − 8`, `85 − 1`). If both D21 cases were deleted: **1688 / 83**
(`1709 − 8 − 13`) — which would silently give back the entire D21 test
contribution, and is the outcome the handoff's warning exists to prevent.

---

## 5. Re-measurement the removal forces

From `INTEGRATION.md:240-270`, unchanged by this survey:

* **Must re-run**: the suite (now one environment, not seven); the D21 family,
  81 models, byte-identity against the current merged build; the 144-run box
  census.
* **Need not re-run**: every `legacy` column in the records (they are
  measurements *of the thing being removed*); D20's corpus, the fold census, the
  CGAL repairs — none reaches the blind branch, two or three byte-identity spot
  checks cover it.
* **Added by this survey**: the four `fillet-tests` / `fillet-tool-tests` image
  baselines, because `LOCALGROUP` flips on. Expected unchanged (both models are
  recorded controls); confirm, do not pre-emptively re-pin.
* **Before the source change**: the 13/13 control, per `handoff-2026-08-03.md:274`.

---

## 6. The one genuinely ambiguous site — needs the owner

**S10 / `arrivesStraight` (`FilletBuilder.cc:2783-2790`, guarded by `localGroup`).**

Turning `LOCALGROUP` on unconditionally also turns `arrivesStraight` on
unconditionally, because it lives inside the flag's own branch. But the final
D17 review (`reviews/D17-FINAL.md`, quoted in `handoff-2026-08-03.md`) recorded
that `arrivesStraight` *"was buying off the grouping's damage and, in doing so,
discarding the largest win in the corpus"* — `bcurve` 20/37 → 0/37, and `mixc`
17/35 → 0/37, `mixp` 9/15 → 0/15 without it. It named the target shape as
**"one idea with one knob"**: no ball at a seam vertex, beads run past each
other, `arrivesStraight` gone.

`INTEGRATION.md:575` then records: **"`arrivesStraight` was not deleted."** It
is still present, still consulted, and was only repaired (to read `rawRun()`
instead of `verts`) at integration. The `anyServed` blocker the review found was
fixed a different way — per chain, at `:3275-3282` — so the review's *blocker*
is closed while its *conclusion about `arrivesStraight`* was never acted on.

The removal pass cannot avoid the question: dropping the flag promotes
`arrivesStraight` from off-by-default to always-on, which is the state the
review measured as costing the corpus's largest single win. The two candidate
answers are:

1. **Ship it** — the conservative reading; the third-pass corpus (7 unsound both
   ways, 17 models moved and all sound) was measured *with* `arrivesStraight`
   present, so it is the configuration that has actually been validated end to
   end on the merged tree.
2. **Delete it with the flag** — the review's own conclusion, but its supporting
   numbers were taken on the pre-integration D17.3 branch, before the
   `rawRun()` fix and before D20 was in the tree, and were never re-taken on
   `integ-three`.

**No record on this branch measures the merged tree with `arrivesStraight`
removed.** This is not a decision the removal pass can make from the evidence in
hand; it needs either the owner's call or that one measurement.

Everything else in §1 and §2 is unambiguous: the shipped value is the current
default in every case.
