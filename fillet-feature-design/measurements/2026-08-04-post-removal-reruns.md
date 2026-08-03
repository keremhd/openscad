# Post-removal re-runs — the 225-model corpus, the 144-run box census, the 81-shape D21 family

Branch `kerem-fillet`. The measurement the env-var removal (`06b301f2d`, recorded
in `env-var-removal.md` at `53874d2f4`) explicitly left open: its §7 names the
225-model corpus as "the single biggest gap", the census and the D21 family as
not re-run, and the "17 of 225 move" figure as unconfirmed.

**Headline: 4 of 225 models move, and none of them gets worse.**

---

## 0. What was compared, and with which binaries

| tag | what it is | executable | size | `strings \| grep -c OPENSCAD_FILLET` |
|---|---|---|---|---|
| `pre` | pre-removal default (`529c5437b`), all five vars unset | rebuilt bundle from `scratchpad/bins/pre_OpenSCAD` | 24308792 | **6** |
| `post` | post-removal default (`06b301f2d` = branch HEAD) | `build/OpenSCAD.app/…/OpenSCAD` | 24306808 | **0** |

The symbol count is discriminating, not vacuous: 6 against 0. Binary mtimes
(01:14:41 and 01:23) are newer than the last source edit (`FilletBuilder.cc`,
01:14:38). Both executables were dropped into fresh copies of the same app
bundle so nothing but the fillet code differs.

**`529c5437b`'s fillet source is byte-identical to `integ-three`:**
`git diff --stat integ-three 529c5437b -- src/geometry/fillet/` is empty. That
is what makes the integration's own committed corpus rows a valid known-answer
reference for `pre`, and it is the reference used below.

Every run has all seven fillet variables explicitly removed from the
environment. Runner is **bash**. The first attempt at the validation sweep ran
under zsh, which does not word-split `$LIST`, so five models became one bogus
filename and the row came back all-`None` — the trap the integration record
warns about, caught by the known-answer check before any real measurement.

### The instrument, and the known-answer case it was validated on

Harness is `integ/sweep.py` from `integ-three:work`, re-pointed at these two
binaries; the analysis function is the integration's own `run.analyse`. Genus,
component count, non-manifold-edge count and volume are **derived from the
exported mesh**, never from ECHO; `osGenus` is recorded alongside as OpenSCAD's
own answer. Refusal counts come from the operator's user-visible WARNING, which
is the only refusal stream that exists in **both** binaries.

**Validated before use, on five models whose answers are already recorded**
(`box_cube_fn48_r1`, `box_L_fn24_r2`, `d21_fn48`, `steinmetz_fn48`,
`tilt60_fn48`): `pre` reproduced `integ/r3-off.jsonl` on **all eleven fields —
`osGenus`, mesh genus, comps, non-manifold edges, volume, md5, refused,
selected, blind, blindRef, redivided — exactly**, md5 included. `post`
reproduced `r3-on.jsonl` on every mesh and refusal field, differing only in
`blind`/`blindRef`/`redivided`, which are the deleted diagnostics and are
flagged `hasGateDebug: false` rather than written as zeros.

Over the full corpus the same check held: **`pre` reproduces `r3-off` on
208 of 208 deterministic models and `post` reproduces `r3-on` on 208 of 208.**

---

## 1. The 225-model corpus — the headline

225 models = the 144-run box census + the 81-shape D21 family. Run end to end
**twice** on each binary (4 × 225 = 900 exports).

| | `pre` (pre-removal default) | `post` (post-removal default) |
|---|---|---|
| exports that failed | **0** | **0** |
| unsound, `osGenus != 0` | **7 of 225** | **7 of 225** (same list) |
| models in more than one component | **0** | **0** |
| models with a non-manifold edge (weld 1e-8) | 113 | **113** |
| creases selected | 7428 | **7428** |
| creases refused | 6887 | **6887** |
| models emitting a refusal warning | 79 | **79** |

The seven unsound are identical in both and are the seven the integration
record already names: `pipes_perp_fn48`, `pipes_perp_fn192`, `sr_fn96_r0.05`,
`sr_fn96_r0.5`, `steinmetz_fn48`, `steinmetz_fn192`, `tee_equal_fn192`. Six are
carried unchanged by clean HEAD; the seventh (`pipes_perp_fn48`) is D20's own
recorded regression. **The removal adds none and repairs none of them.**

### Movement, and the nondeterminism it hides

| pass | md5 movers, `pre` vs `post` |
|---|---|
| pass 1 | 19 of 225 |
| pass 2 | 19 of 225 |
| in both passes | 17 |
| **in both passes and provably deterministic** | **4** |

Determinism was established by running each binary over the whole corpus twice
and comparing md5 to itself. **17 models are nondeterministic in vertex order**
— all 15 the integration recorded, plus **two the record does not list**:

* `box_L_fn96_r1`
* `box_T_fn96_r1`

Both are pure vertex-order noise: genus, component count, non-manifold-edge
count and volume are identical between passes and between binaries. `box_L_fn96_r1`
is one of the five models the integration record counted as a real flag-on
mover, so **the record's own "5 of 210 deterministic" is itself one high.**

### The record's "17 of 225" — confirmed as a number, and corrected as a claim

Recomputing the record's own figure from `r3-off.jsonl` vs `r3-on.jsonl`: 17
movers, 12 of them nondeterministic, deterministic subset
`{boss_on_plate_fn192, box_L_fn96_r1, box_step_fn24_r3, box_step_fn48_r3,
box_step_fn96_r3}`.

My post-removal measurement reproduces that set exactly, minus `box_L_fn96_r1`,
which I show is nondeterministic. So:

* **"17 of 225 move" is right as a raw md5 count and was not understated.**
* The *real* count of models the removal moves is **4**, not 17 and not 5.
* The implementer's 4-of-13 controls moving is consistent with this: `pocket`,
  `rhomb`, `slab` and `corner` are not in the 225-model corpus at all.

### Every mover, before and after

Genus is the mesh's own, at weld `1e-8 × bbox diagonal`; `osGenus` is
OpenSCAD's. `nme` = edges carried by other than two faces after that weld.

| model | family | osGenus pre → post | mesh genus pre → post | comps pre → post | nme pre → post | volume pre | volume post | rel Δvol | refused/selected pre → post | verdict |
|---|---|---|---|---|---|---|---|---|---|---|
| `boss_on_plate_fn192` | 81 shapes | 0 → 0 | −44 → **−20** | 1 → 1 | 88 → **40** | 10027.210128 | 10027.806779 | 5.95e−05 | 4/20 → 4/20 | **BETTER** |
| `box_step_fn24_r3` | census | 0 → 0 | 0 → 0 | 1 → 1 | 0 → 0 | 8967.360102 | 8968.585388 | 1.37e−04 | 6/18 → 6/18 | same soundness |
| `box_step_fn48_r3` | census | 0 → 0 | −1 → −1 | 1 → 1 | 2 → 2 | 8975.057748 | 8976.134587 | 1.20e−04 | 6/18 → 6/18 | same soundness |
| `box_step_fn96_r3` | census | 0 → 0 | −5 → −5 | 1 → 1 | 8 → 8 | 8976.998621 | 8978.019911 | 1.14e−04 | 6/18 → 6/18 | same soundness |

The thirteen nondeterministic md5 movers are listed for completeness — every one
has **identical** osGenus, comps, nme and volume (to better than 1e−12
relative) before and after, i.e. vertex order only:

`box_L_fn96_r0.5`, `box_L_fn96_r1`, `box_T_fn96_r0.5`, `box_T_fn96_r1`,
`box_cross_fn96_r0.5`, `box_cross_fn96_r1`, `box_cross_fn96_r1.5`,
`box_cube_fn96_r0.5`, `box_cube_fn96_r1`, `box_cube_fn96_r1.5`,
`box_cube_fn96_r2`, `box_cube_fn96_r3`, `box_ribs_fn96_r0.5`,
`rounded_cube_fn96`, `rounded_cube_fn192`, `sop2_fn192`.

### **Does any model get worse? No.**

Scored on every one of the four criteria the brief names — genus away from 0,
new non-manifold edges, more components, a failed or open export — across all
225 models:

* **0 models get worse on any criterion.**
* **1 model gets better**: `boss_on_plate_fn192` halves its non-manifold-edge
  count, 88 → 40, and its mesh genus moves from −44 to −20, i.e. **towards** 0.
* 3 models move by a volume of order 1e−4 relative with every soundness metric
  unchanged.
* 0 models change component count. 0 exports fail. 0 models newly acquire a
  non-manifold edge.
* The 7-model unsound list is identical before and after.

### Weld tolerance — stated, and the direction checked at four of them

The brief's warning is real: on `boss_on_plate_fn192` the non-manifold-edge
count runs 88 → 136 → 1136 as the weld coarsens from 1e−7 to 1e−6 to 1e−5. So
the metric is read **for direction and for zero only**, and the direction is
checked at every tolerance:

| model | weld 1e−8 | 1e−7 | 1e−6 | 1e−5 |
|---|---|---|---|---|
| `boss_on_plate_fn192` nme, pre → post | 88 → **40** | 88 → **40** | 136 → **72** | 1136 → **872** |
| `box_step_fn24_r3` | 0 → 0 | 0 → 0 | 0 → 0 | 4 → 4 |
| `box_step_fn48_r3` | 2 → 2 | 2 → 2 | 4 → 4 | 48 → 48 |
| `box_step_fn96_r3` | 8 → 8 | 8 → 8 | 12 → 12 | 222 → **174** |
| `box_cube_fn24_r1` (byte-identical control) | 0 → 0 | 0 → 0 | 0 → 0 | 0 → 0 |
| `steinmetz_fn48` (known −1 residual) | −1 → −1 | −1 → −1 | −1 → −1 | −1 → −1 |

**Post is never worse than pre at any tolerance, and is strictly better at every
tolerance on `boss_on_plate_fn192`.** The two controls are what say the
instrument's zero is a real zero and its nonzero a real nonzero: a
byte-identical model reads 0 at all four welds, and a model with a recorded
pre-existing −1 reads −1 at all four.

---

## 2. The 144-run box census

144 models (`$fn` {24,48,96} × `r` {0.5,1,1.5,2,2.5,3} × 8 bodies), 130 of them
deterministic under these two binaries.

| | `pre` | `post` |
|---|---|---|
| unsound (`osGenus != 0`) | **0 of 144** | **0 of 144** |
| models in more than one component | 0 | 0 |
| exports failed | 0 | 0 |
| **creases refused** | **300** | **300** |
| creases selected | 549 | 549 |
| models emitting a refusal warning | 27 | 27 |
| models with a non-manifold edge (weld 1e−8) | 62 | 62 |
| md5 movers vs the other binary, pass 1 / pass 2 | 15 / 16 | — |
| **reproducible deterministic movers** | **3** | `box_step_fn24_r3`, `box_step_fn48_r3`, `box_step_fn96_r3` |
| byte-identity to the record | 130/130 vs `r3-off` | 130/130 vs `r3-on` |

**The 300-crease refusal figure the census exists to protect is unmoved.** So is
the false-refusal repair: on the `pre` binary, which still carries the
diagnostic, the census shows **498 blind chains and 0 blind refusals** and **54
creases re-divided** — every one of those four numbers identical to the
integration record. The census is 0 unsound before and after, and no model on it
gets worse.

## 3. The 81-shape D21 family

81 models, 78 of them deterministic.

| | `pre` | `post` |
|---|---|---|
| unsound (`osGenus != 0`) | **7 of 81** | **7 of 81**, same list |
| models in more than one component | 0 | 0 |
| exports failed | 0 | 0 |
| creases refused | 6587 | 6587 |
| creases selected | 6879 | 6879 |
| models emitting a refusal warning | 52 | 52 |
| models with a non-manifold edge (weld 1e−8) | 51 | 51 |
| md5 movers, pass 1 / pass 2 | 4 / 3 | — |
| **reproducible deterministic movers** | **1** | `boss_on_plate_fn192`, and it improves |
| byte-identity to the record | 78/78 vs `r3-off` | 78/78 vs `r3-on` |

The seven-unsound figure the integration record reports for `integ-three`
(`pipes_perp_fn48`, `pipes_perp_fn192`, `sr_fn96_r0.05`, `sr_fn96_r0.5`,
`steinmetz_fn48`, `steinmetz_fn192`, `tee_equal_fn192`) is reproduced exactly,
and the removal changes it in neither direction. On the `pre` binary the family
shows **5286 blind chains, 4924 blind refusals and 1649 creases re-divided** —
both mechanisms firing hard on this corpus, which is what makes "no outcome
moved" a statement about the removal rather than about an idle feature.

---

## 4. Did the feature fire? — counts from the shipped binaries

Read from the user-visible warning stream, which exists in both binaries:

| | `pre` | `post` |
|---|---|---|
| creases selected across the 225 | 7428 | **7428** |
| creases refused across the 225 | 6887 | **6887** |
| models emitting at least one refusal warning | 79 | **79** |

The size gate refuses 6887 of 7428 creases in the shipped post-removal binary.
This is not byte-identical inertness misread as a win: four models move, 6887
refusals fire, and the mesh outcome of every one of the 225 is at least as sound
as before.

### The gate's internal counts, from a temporarily instrumented build

**These numbers, and only these, come from an instrumented binary.** The
`FILLETGATE` and `FILLETRESAMPLE` prints were re-added to `FilletBuilder.cc` in
the working tree, unconditionally (no environment variable), the binary was
built and copied out, and **the source was reverted before anything else was
done**. `git status` and `git diff` are empty; the instrumentation is not on
`kerem-fillet`. The instrumented binary carries 0 `OPENSCAD_FILLET` symbols and
2 diagnostic strings, so it is the shipped code plus prints and nothing else.

**Proof the instrumentation is geometrically inert**: the instrumented build is
byte-identical to the uninstrumented `post` build on **208 of 208** deterministic
models, no exceptions. So the counts below belong to the shipped binary's
behaviour, not to the instrumented one's.

| | `pre` (its own diagnostic) | `post` (instrumented) |
|---|---|---|
| **225 corpus** — gate chains | 10378 | **10378** |
| blind chains | 5784 | **5784** |
| blind refusals | 4924 | **4924** |
| creases re-divided | 1703 | **1703** |
| creases refused / selected | 6887 / 7428 | **6887 / 7428** |
| **144 census** — gate chains | 2970 | **2970** |
| blind chains | 498 | **498** |
| **blind refusals** | **0** | **0** |
| creases re-divided | 54 | **54** |
| creases refused / selected | 300 / 549 | **300 / 549** |
| **81 shapes** — gate chains | 7408 | **7408** |
| blind chains | 5286 | **5286** |
| blind refusals | 4924 | **4924** |
| creases re-divided | 1649 | **1649** |
| creases refused / selected | 6587 / 6879 | **6587 / 6879** |

**Every count is identical before and after the removal.** The census's
498-blind-chains / 0-blind-refusals pair — the epsilon repair the review forced —
is intact in the shipped binary and is now measured there rather than inferred.
The gate is not merely present; it examines 10378 chains, of which 5784 are
blind, and refuses 4924 of those.

---

## 5. Excluded, and why

| excluded | reason |
|---|---|
| `cone_fn192` | peaks at 156 GB under CGAL and OOMs in every binary including clean base. Not in the 225 corpus; not run. |
| `cone_fn384`, `sph_across_fn384` | cannot be exported at all on this machine. Not in the 225 corpus; not run. |
| `d21_fn384`, `falseacc384` | present in `corpus/` but **not** among the 225 rows the integration recorded, so excluded to keep the comparison exactly like-for-like. 227 `.scad` files, 225 measured. |

Nothing else was skipped. All 225 exported successfully under both binaries in
both passes; there are no silent gaps in the tables above.

---

## 6. What was NOT measured

* **Post-removal gate counts from the *shipped* binary.** They cannot be read
  from it at all — `FILLETGATE` and `FILLETRESAMPLE` were deleted — so in the
  §1–§3 tables `post`'s `blind`/`blindRef`/`redivided` are recorded as
  *unavailable*, not as zero (`hasGateDebug: false`), and every blind-chain and
  re-division figure there is from the **`pre`** binary. The post-removal counts
  in §4 come from a **temporarily instrumented** build, reverted immediately;
  they are proven equivalent to the shipped one by 208/208 byte-identity, but
  they are not a read of the shipped binary itself.
* **No clean-`944e0cbef` baseline.** The comparison is pre-removal default
  against post-removal default, which is the question the removal poses. The
  "byte-identical to clean HEAD" counts were not re-derived.
* **The `$fn` and scale sweeps** were not re-run.
* **The fold census, the D19 ledge, the D17.3 oblique sweep** were not re-run.
* **No CGAL-backend run.** Everything here is the Manifold backend.
* **No performance measurement.** The cost of the unconditional `arrivesStraight`
  pass is still untimed.
* **`arrivesStraight` removed** is still unmeasured on the merged tree.
* **The 13 controls** were not re-run; the implementer's rows stand.
* Only **two** passes per binary. Three of the seventeen nondeterministic models
  happened to agree between passes on one binary or the other, so the
  nondeterministic set may be larger than seventeen. It cannot be smaller, and
  every model in it has identical invariants, so no conclusion above depends on
  the boundary.
