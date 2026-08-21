# `arrivesStraight` — what it does, and what removing it costs on the merged tree

Branch `worktree-agent-a1065db1b78cc5fd6`, worktree
`/Users/kerem/Devel/openscad/.claude/worktrees/agent-a1065db1b78cc5fd6`, from
`kerem-fillet` HEAD `7f5f35cc4`.

The measurement `removal-scope.md` §6, `env-var-removal.md` §7 and
`2026-08-04-post-removal-reruns.md` §6 all leave open: **the merged tree with
`arrivesStraight` removed.** It has never been taken. `06b301f2d` moved the
predicate from off-by-default to always-live without it ever being measured in
that state.

---

# DELIVERABLE A — what `arrivesStraight` actually does

## In one sentence

`arrivesStraight` asks whether a crease reaches its own endpoint in a straight
line, and where the answer is no it **switches that corner back to the old
construction** — the seated corner ball — instead of letting the new "seam
rule" run two beads past the corner.

## The geometry, plainly

The fillet builder has two ways to finish a corner where two filleted creases
meet:

1. **The seated ball** (the original). Solve for a ball that sits against the
   walls at the corner, cut the incoming beads back to it, and let the ball fill
   the gap. This is what every corner used before the seam feature.

2. **The seam rule** (the new one, from D17.3). At a corner that an *unfilleted*
   crease also leaves — a "seam vertex", typically because a brush deliberately
   left that crease sharp — there is no sensible corner cell to build. So
   instead: build no ball, and let the two beads simply **run past** the corner
   a short distance (`seamOver`, a tenth of a radius) so they overlap each other
   and close the gap themselves.

Step 2 runs each bead past the vertex **along the straight line its last segment
lies on**. That is only the crease's own continuation if the crease was
*straight* when it arrived. If the crease arrives on a **curve** — the classic
case being the tessellated base arc where a cylindrical boss meets a plate — the
last segment is a **chord** of that circle, and a chord produced past its end
leaves the circle **on the outside**. The top of the bead's cross-section is
tangent to the curved wall, so extending it along the chord lays the bead a hair
*outside* the wall it was supposed to be tangent to. The tangency becomes a
sliver of surface resting on the wall at a hair of an angle — which is exactly
the degeneracy the overrun existed to remove, reintroduced slightly further
along. In the mesh it shows up as edges carried by four faces.

`arrivesStraight` detects that case and hands the vertex back to construction 1,
which has no such problem because a seated ball is solved against whatever walls
it actually finds.

## The code

`FilletBuilder.cc:2299` (predicate) and `:2718–2726` (its only consumer).

```
bool arrivesStraight(const MergedMesh& m, const Chain& c, bool front)
```

It takes the chain's **raw mesh run** (`c.rawRun()`, not `c.verts` — that is
D20's fix; after D20's resampler `verts` carries `-1` for interpolated stations
and indexing `m.pos[-1]` is an out-of-bounds read), takes the last three mesh
vertices at the requested end, and returns true iff the two consecutive edge
vectors are collinear:

```
a.dot(b) / (la * lb) > 1.0 - 1e-9
```

A run of fewer than three vertices returns true — a single-segment crease is a
straight line, with nothing to check.

Its result feeds exactly one thing:

```
seamVertex(v)  ==  arrivesBent.count(v) == 0  &&  creaseLeavesUnfilleted(...)
```

and `seamVertex` is what decides whether a vertex is withheld from `junctionAt`
— i.e. whether it gets the seam rule (no corner cell, beads run past) or the
ordinary seated ball. So **`arrivesStraight` gates the whole seam rule at that
vertex, not merely the overrun.** The D17.3 record is explicit that this was
deliberate: "It gates the whole rule, not only the overrun, because layer 1
alone was bad on `rib.scad` at ten tessellations too."

## Which defect introduced it, and what problem it was written to solve

**D17.3, the "brushed corner" defect** (`git show fix-blockers:work/BLOCKERS-AB.md`,
section "The fix"). It was not part of the original D17.3 design; it was added
*during* the work, as a repair for damage the seam rule itself was doing.

The problem it was written to solve is named exactly, and it is **one model**:

> | `rib.scad`, `$fn` 16..160 step 4 | bad tessellations | worst | which |
> | HEAD | 1 / 37 | 1 | 64 |
> | candidate BEFORE | 9 / 37 | 23 | 28 36 44 56 60 64 80 116 136 |
> | candidate AFTER  | **1 / 37** | 1 | 64 |

The record's own summing-up is unusually candid, and is the single most
important sentence for deciding its fate:

> Recorded honestly: it is described as a general geometric predicate and is not
> one. What it does on this corpus is "the crease is one segment, or its last two
> segments are exactly collinear". **It earns its lines by keeping one model off
> nine bad tessellations, not by generalising.**

and:

> That is the whole case for keeping it, and it is a case about `rib.scad` alone.

It also records that the predicate is, in practice, a *planar-single-segment*
rule: it fires at 0 of 6 candidate seam vertices on curved models, all 45/45
served vertices corpus-wide have a single-segment arrival, and **no cosine
anywhere in the corpus lands inside the 1e-9 band**, so the constant "could be
1e-3 or 1e-15 unchanged".

## So: can its purpose be established?

**Yes — unusually well, in fact.** Both the geometric reasoning and the defect
that introduced it are written down clearly and survive reading the code. This
is *not* a mechanism nobody can explain.

But there are two things it is important not to confuse:

* **What it is for** is establishable and is stated above.
* **Whether it is still doing that job** is a separate question, and the D17
  adversarial review already challenged it: it argued the badness the gate buys
  off is *the per-chain grouping's*, not the overrun's, and that with the
  subtraction left global the gate is unnecessary on `rib.scad` and actively
  harmful elsewhere.

That second question is Deliverable B, and it is where the answer has changed.

## The trap in the review's recommendation — read this before reusing its numbers

The D17 final review (`fillet-feature-design/reviews/D17-FINAL.md`, Attack 3)
recommended deleting `arrivesStraight`, citing:

| `$fn` 16..160 step 4 | HEAD | shipped | ungated + global |
|---|---|---|---|
| `bcurve.scad` | 20 / 37, worst 5 | 20 / 37 | **0 / 37** |
| `mixc.scad` | 17 / 35, worst 5 | 26 / 35, worst 52 | **0 / 37** |
| `mixp.scad`, 15 cells | 9 / 15 | 13 / 15, worst 18 | **0 / 15** |

**That `0/37` column is `ungated + global` — TWO changes, not one.** It is
`arrivesStraight` deleted *and* the per-chain grouping reverted to a global
subtraction. The review's recommendation was a package: delete the grouping,
`anyServed` and `arrivesStraight` together.

**Integration shipped the opposite half of that package.** `06b301f2d` removed
`OPENSCAD_FILLET_LOCALGROUP` by making local grouping **unconditional and
permanent**. So deleting `arrivesStraight` on the current tree produces
`ungated + LOCAL`, a combination the review never put in a column — and the one
cell of that combination it did measure (`mixc` "shipped" is gated+local at
26/35 worst 52) is its worst.

Anyone quoting "0/37" as the expected result of deleting `arrivesStraight` today
is quoting a number for a tree that no longer exists.

---

# DELIVERABLE B — the measurement

## Which arm "removed" means, and why

`arrivesStraight` is a **gate**: when it returns false the vertex is added to
`arrivesBent` and is thereby **excluded** from the seam rule. Deleting a gate
means the gate stops blocking. So the "removed" arm is:

* the function deleted;
* the `arrivesBent` set and the loop that fills it deleted;
* `seamVertex` reduced to `creaseLeavesUnfilleted(...)` alone.

Equivalently: `arrivesStraight` always true, `arrivesBent` always empty, the
seam rule applies wherever an unfilleted crease leaves the vertex. This is what
`OPENSCAD_FILLET_NOSTRAIGHT=1` did in the review's throwaway build ("disables
only the `arrivesStraight` gate"), and it is what the review meant by deleting
it.

The brief's parenthetical — "the condition it guards taken the way it goes when
`arrivesStraight` is false" — reads the other way (always insert into
`arrivesBent`, seam rule never applies anywhere). That is not removal; it is
disabling the entire D17.3 feature, and it cannot be what a review claiming the
change *recovers the largest win in the corpus* was asking for. The arm built
here is the always-true one. Diff: 47 deletions, 1 insertion, in
`src/geometry/fillet/FilletBuilder.cc` only.

## Binaries — both built in this worktree, mtimes verified

| arm | source | binary mtime | size | md5 |
|---|---|---|---|---|
| `kept` | `7f5f35cc4` unmodified | **4 aug 10:56:35 2026** | 24289736 | `e86ba8aa…` |
| `removed` | `7f5f35cc4` + the deletion above | **4 aug 10:58:31 2026** | 24289576 | `9663bb9d…` |

`FilletBuilder.cc` mtimes: 10:51:xx (pre-edit, the source `kept` was built from)
and **10:57:28** (post-edit, the source `removed` was built from). Each binary's
mtime is newer than the source it was built from. Both were built with
`make -C build -j8 OpenSCADExe` (the GUI target is `OpenSCADExe`, not
`OpenSCAD`) and each was confirmed to exist by `ls` with a size, not by trusting
"Built target". `CCACHE_BASEDIR=/Users/kerem/Devel/openscad` was set for both.
The two differ by 160 bytes and by md5, so they are not the same build.

**The main checkout was never written to.** Everything here is this worktree
plus two read-only scratchpads.

## Proof the two binaries really differ (the inertness rule)

At default parameters, exported OFF compared directly:

| model | kept | removed | |
|---|---|---|---|
| `bcurve` | nonman 5, tris 3496, vol 21748.293961 | nonman 0, tris 3020, vol 21749.311205 | **DIFFER** |
| `mixc` | nonman 4, tris 3782, vol 24513.194829 | nonman 0, tris 3308, vol 24514.212072 | **DIFFER** |
| `mixp` | nonman 0, tris 1702, vol 31241.832462 | nonman 2, tris 1386, vol 31245.889739 | **DIFFER** |

Not byte-identical inertness. The predicate is live and changes real geometry —
and note it moves in **both directions** even in these three rows.

## Instrument validation (the known-answer rule)

Two independent instruments are used, and both were validated before use.

**1. `run.analyse`** (genus / components / non-manifold edges / volume derived
from the exported mesh, weld `1e-8 × bbox diagonal`) via `sweep2.py`, which is
`integ-three:work/integ/sweep.py` re-pointed at this session's scratchpad.

Validated on the **five models whose answers are already recorded** in
`integ/r3-on.jsonl` — the same five the post-removal record validated on
(`box_cube_fn48_r1`, `box_L_fn24_r2`, `d21_fn48`, `steinmetz_fn48`,
`tilt60_fn48`). The `kept` arm reproduces `r3-on` on **osGenus, mesh genus,
comps, non-manifold edges, refused/selected, volume and md5 — all eight fields,
all five models, exactly.** A sixth confirmation fell out of the corpus run:
`boss_on_plate_fn192` reads genus −20 / nme 40 on `kept`, which is precisely the
`post` value recorded in `2026-08-04-post-removal-reruns.md`.

**So yes — the post-removal record's "kept" arm reproduces here.**

**2. `mesh2.report`** (the D17 review's own checker, `fix-blockers:work/ab/mesh2.py`),
used for the bad-tessellation metric so the review's numbers are re-taken in
their own units rather than in a substitute.

### What the review's metric actually is — confirmed, not assumed

Established by reading `fix-blockers:work/ab/ribsweep.sh` and `sweep15.sh`:

* **Denominator** = number of rows in the sweep. `seq 16 4 160` is **37** `$fn`
  rows; the 15-cell grid is TH ∈ {60,75,90,105,120} × RR ∈ {1,2,3}.
* **Criterion**: a row is "bad" iff `mesh2.report(off)['nonman'] != 0`, where
  `nonman` counts edges carried by **more than two** faces after welding.
* **Weld tolerance**: `mesh2.weld` uses **absolute `tol=1e-7`**, *not* relative.
  This differs from `run.analyse`'s `1e-8 × diagonal`; both are reported below
  and the metric is read **for direction and for zero only**, per the brief.
* "worst" = max `nonman` over the rows.

`bcurve.scad`, `mixc.scad` and `mixp.scad` all already set
`$fa=360/FN; $fs=0.01;`, so the crease threshold tracks `$fn` on them.
**`rib.scad` does not** — it sets only `$fn`, so for `FN >= 30` its threshold is
pinned at `1.5 × max(12, 360/FN) = 18°` regardless of `$fn`. That is the trap
the brief warns about, and it is baked into the original D17.3 measurement.
`rib.scad` is run here unmodified so the numbers compare to the record; the
caveat is recorded rather than silently corrected.

---

## Result 1 — `rib.scad`: the case for keeping it has evaporated

`rib.scad` is the **only** model the D17.3 record offers as justification
("it is a case about `rib.scad` alone").

| `rib.scad`, `$fn` 16..160 step 4 | bad | worst | which |
|---|---|---|---|
| D17.3 record, HEAD | 1 / 37 | 1 | 64 |
| D17.3 record, gate deleted | 9 / 37 | 23 | 28 36 44 56 60 64 80 116 136 |
| **this tree, `kept`** | **3 / 37** | **2** | 20=1 68=2 80=2 |
| **this tree, `removed`** | **3 / 37** | **2** | 20=1 68=2 80=2 |

**All 37 rows are byte-identical between the two arms** (md5 compared per row,
37/37 identical).

On the current merged tree `arrivesStraight` is **completely inert on
`rib.scad`** — the one model it was written for, and the whole of its recorded
justification. The 9/37-vs-1/37 penalty for deleting it does not reproduce; it
does not exist any more. The tree has moved underneath it: `rawRun()` replaced
`Chain::verts` in both `filletedEdges` and `arrivesStraight`, and D20's
resampler changed what the chains are.

## Result 2 — the three models the D17 review cited

Review's own metric, both arms, this tree:

| sweep | rows | `kept` | `removed` | rows byte-identical |
|---|---|---|---|---|
| `bcurve.scad`, `$fn` 16..160 | 37 | 13 / 37, worst 5 | **1 / 37, worst 5** | 0 / 37 |
| `mixc.scad`, `$fn` 16..160 | 37 | 14 / 37, worst 5 | **1 / 37, worst 2** | 0 / 37 |
| `mixp.scad`, 15 cells, TW=1 | 15 | **4 / 15, worst 2** | 10 / 15, worst 4 | 0 / 15 |
| `mixp.scad`, 15 cells, TW=4 | 15 | **4 / 15, worst 1** | 12 / 15, worst 3 | 0 / 15 |

Cell by cell:

| sweep | cells removal makes WORSE | cells removal makes BETTER |
|---|---|---|
| `rib` | 0 | 0 |
| `bcurve` | 1 (`$fn` 32) | 13 |
| `mixc` | 1 (`$fn` 32) | 14 |
| `mixp` TW=1 | 9 | 2 |
| `mixp` TW=4 | 10 | 1 |

(`mixp`'s TW is not recorded in the review; the cited invocation is
`thsweep.sh work/rev2/mixp.scad p1 1`, whose third argument is TW by analogy
with `sweep15.sh`. Both TW=1 and the model's own default TW=4 are run, and they
agree in direction, so the conclusion does not depend on resolving it.)

### Does the review's claimed corpus win reproduce?

**Directionally yes on the curved models, and it is large — but it does not
reach the review's `0/37`, and it does not hold on the planar one.**

* `bcurve` 13 → **1** and `mixc` 14 → **1** are big, real repairs. Both land on
  a single residual bad row (`$fn` 32) rather than the review's clean zero —
  which is exactly what the "ungated + global" caveat above predicts, since the
  grouping half of the review's package is now permanent and cannot be reverted
  from this tree.
* `mixp` goes **4 → 10** (TW=1) and **4 → 12** (TW=4). This is a **clear
  regression**, and it is the direct measurement the review never took: `mixp`
  is the *mixed* model, the one built specifically to exercise a model carrying
  both served and withheld seam vertices — i.e. the grouping. With the grouping
  now permanent, removing the gate hurts it.
* The review's absolute numbers do **not** re-take: it reported `bcurve` 20/37
  and `mixc` 17/35 for HEAD and 26/35 for shipped; this tree reads 13/37 and
  14/37 for `kept`. Expected — `rawRun()`, D20 and D21 have all landed since.
  The record's numbers should not be quoted against this tree.

---

## Proof the two arms are the builds claimed — symbol level, not just bytes

The rule that earned its place here (one reported win in this effort was
byte-identical inertness) demands this be settled hard, because part of the
result below *is* an inertness finding.

| binary | size | mtime | `arrivesStraight` symbol (`nm -a`) | `FILLETSTRAIGHT` string | `OPENSCAD_FILLET` strings |
|---|---|---|---|---|---|
| `kept` | 24289736 | 4 aug 10:56:35 | **1** | 0 | 0 |
| `removed` | 24289576 | 4 aug 10:58:31 | **0** | 0 | 0 |
| `instr` | 24289736 | 4 aug 11:10:01 | 1 | **1** | 0 |

md5: `e86ba8aa…` / `9663bb9d…` / `04b52d66…` — three distinct builds.

**The `arrivesStraight` symbol is present in `kept` and absent from `removed`.**
That is direct evidence the deletion compiled into the binary, not merely into
the source. All three carry zero `OPENSCAD_FILLET` env strings, confirming all
are post-`06b301f2d` trees.

## How often the gate fires — from a temporarily instrumented build

`instr` is `kept` plus one `printf` of the seam-vertex census
(`ends` / `cand` / `bent` / `served`), where `cand` = distinct open-chain end
vertices that an unfilleted crease also leaves (what the seam rule would serve
if the gate were deleted), `bent` = those `arrivesStraight` withholds, `served`
= those it lets through. **The instrumentation was reverted immediately**;
`git status` is clean and it is not on this branch.

`instr` is byte-identical to `kept` on all four models below, so the census
belongs to the shipped code's behaviour, not the instrumented one's.

| model | `instr` vs `kept` | ends | cand | bent | served |
|---|---|---|---|---|---|
| `rib` (`$fn` 128) | IDENT | 2 | 2 | **2** | 0 |
| `bcurve` | IDENT | 2 | 2 | **2** | 0 |
| `mixc` | IDENT | 6 | 6 | **2** | 4 |
| `mixp` | IDENT | 6 | 6 | **2** | 4 |

**The gate fires on all four**, withholding 2 vertices on each. It is not dead
code.

## Result 3 — the 225-model corpus

144 census + 81 shapes, **two full passes per arm** (4 × 225 = 900 exports), so
vertex-order nondeterminism is separated from real movement.

| | `kept` | `removed` |
|---|---|---|
| exports failed | 0 | 0 |
| unsound (`osGenus != 0`) | 7 of 225 | **7 of 225, same list** |
| models in >1 component | 0 | 0 |
| models with a non-manifold edge (weld 1e−8 rel) | 113 | **114** |
| creases selected | 7428 | **7428** |
| creases refused | 6887 | **6887** |
| md5 movers, pass 1 / pass 2 | 18 / 19 | — |
| deterministic on both binaries | 209 of 225 | — |
| **real movers** (move in both passes AND deterministic) | **3** | — |

The seven unsound are the seven already on record (`pipes_perp_fn48/192`,
`sr_fn96_r0.05/0.5`, `steinmetz_fn48/192`, `tee_equal_fn192`); removal adds none
and repairs none. 16 models are nondeterministic in vertex order, including
`box_L_fn96_r1` and `box_T_fn96_r1` — consistent with the post-removal record's
correction to the recorded list of 15.

### Every real mover — and all three get WORSE

| model | osGenus | mesh genus | comps | nme | volume | refused/selected |
|---|---|---|---|---|---|---|
| `box_step_fn24_r3` | 0 → 0 | **0 → −2** | 1 → 1 | **0 → 4** | 8968.585388 → 8969.815220 | 6/18 → 6/18 |
| `box_step_fn48_r3` | 0 → 0 | **−1 → −4** | 1 → 1 | **2 → 8** | 8976.134587 → 8977.211766 | 6/18 → 6/18 |
| `box_step_fn96_r3` | 0 → 0 | **−5 → −15** | 1 → 1 | **8 → 28** | 8978.019911 → 8979.042040 | 6/18 → 6/18 |

**MODELS WORSE WITH IT REMOVED: 3. MODELS BETTER: 0.**

`box_step_fn24_r3` goes from a *clean* mesh (genus 0, 0 non-manifold edges) to a
damaged one. And the damage **grows with refinement** — nme 4 → 8 → 28 across
`$fn` 24 → 48 → 96, genus −2 → −4 → −15. By the owner's own first standard
("a defect is real only if refining `$fn` does not fix it"), this is a **real
defect, not faceting**.

## Result 4 — the 13 controls

Both arms, two passes each, using the form the controls doc itself recommends
(byte-identity against the same tree *without* the change under test):

**All 13 byte-identical between `kept` and `removed`, in both passes.**
0 movers, 0 nondeterministic, 0 unsound, 0 non-manifold edges, refused/selected
2/3 on both arms (the single `rib` blind refusal). The gate is inert on the
entire control set.

(Note: the brief's "`boss.scad` and `ctrl1.scad` are byte-identical" does not
hold for the *sources* — all 13 control `.scad` files have distinct md5s. Their
exports are separately identical between arms, so nothing here depends on it.)

---

# The answer to the question that decides it

### Does removing it make anything WORSE? **Yes — unambiguously.**

* 3 of 225 corpus models regress, 0 improve. `box_step_fn24_r3` goes from a
  clean mesh to a damaged one, and the damage grows under refinement.
* `mixp` regresses hard: 4/15 → 10/15 (TW=1) and 4/15 → 12/15 (TW=4).

### Does it recover the corpus win the review claimed? **Partly, and not where the review said.**

* On the two **curved** models the direction reproduces and the effect is large:
  `bcurve` 13/37 → **1/37**, `mixc` 14/37 → **1/37**.
* It does **not** reach the review's `0/37`, and it was never going to: that
  column was `ungated + global`, and the global-subtraction half of the package
  was permanently removed in the opposite direction by `06b301f2d`.
* On the 225-model corpus — the thing "largest win in the corpus" refers to —
  there is **no win at all**. Net effect is 3 worse, 0 better.

### The finding that most changes the picture

**`arrivesStraight` is completely inert on `rib.scad`, the one and only model
its existence was ever justified by**, at all 37 tessellations, byte-identical.
Yet the census shows it *fires* there (cand 2, bent 2, served 0). So the gate
still withholds both vertices on `rib` — withholding them simply no longer
changes the geometry. The D17.3 record's decisive number (deleting it costs
`rib` 9/37 bad tessellations, worst 23) **does not reproduce on this tree**:
both arms read 3/37, worst 2, identical rows.

The tree moved underneath the justification — `rawRun()` replaced
`Chain::verts` in both `filletedEdges` and `arrivesStraight`, and D20's
resampler changed what the chains are.

So the situation is now precisely inverted from the record:

* The model it was **written for** no longer needs it.
* The models it now protects (`box_step_*_r3`, `mixp`) are ones **nobody chose
  it for**, and it protects them **by accident**.
* The models it now **damages** (`bcurve`, `mixc`) are the ones the review
  identified.

# RECOMMENDATION — **keep it for now; do not delete it on this evidence**

Deleting `arrivesStraight` today is a **false acceptance**: it takes three
currently-clean or nearly-clean corpus models and shatters them, worse under
refinement, in exchange for a win on two models that are not in the corpus. The
owner's standard is explicit — *a valid solid with an unfilleted crease beats a
shattered one; a false refusal is the safe error, a false acceptance is the
dangerous one.* `arrivesStraight` is a refusal mechanism. On this tree it is
refusing in three places where refusing is currently correct.

That is a recommendation about **this tree**, not an endorsement of the
mechanism. Its own author's verdict stands — it is described as a general
predicate and is not one, and its stated justification has now expired. But the
right sequencing is:

**Cannot tell without X** — where X is *the review's actual recommendation,
measured whole*. The review never proposed deleting `arrivesStraight` alone; it
proposed deleting the per-chain grouping, `anyServed` **and** `arrivesStraight`
together, and every `0/37` it reports is from that combination. Integration
shipped the grouping half permanently in the opposite direction and then read
the review's conclusion as if it applied to the remaining third. **The open
question is not "should `arrivesStraight` go" but "should the local grouping
have been made permanent".** Until a build exists with the grouping global
again, the review's claim cannot be tested and `arrivesStraight` cannot be
fairly judged, because everything it is currently buying off is grouping damage.

Concretely, the next measurement worth taking is a three-arm build:
`kept` / `removed` / `removed + global subtraction`, over these same corpora. If
the third arm puts `box_step_*_r3` and `mixp` back while keeping `bcurve` and
`mixc` at 1/37, the review is vindicated in full and both mechanisms go
together. That build was out of scope here and is **not** measured.

---

# What was NOT measured

* **`removed + global subtraction`** — the review's actual recommendation. The
  global branch was deleted with the env var, so measuring it needs new code,
  not a flag. **This is the gap that decides the question**, and it is open.
* **The opposite arm** ("always bent", seam rule never applies). Not built; it
  is not what "removed" means, but it would show how much the whole D17.3
  feature is worth on this tree.
* **`blind` / `re-divided` counts** for the movers. `FILLETGATE` and
  `FILLETRESAMPLE` were deleted in `06b301f2d`, so neither arm can report them;
  they are size-gate/resampler diagnostics and `arrivesStraight` does not touch
  either mechanism. `served` is reported instead (§ census), which is the count
  this predicate actually governs. Refused/selected come from the user-visible
  warning and are reported for every model.
* **No performance timing.** The cost of the now-unconditional
  `arrivesStraight` pass over every open chain is still untimed — carried
  forward unresolved from `env-var-removal.md` §7.
* **No unit-test run** on the `removed` arm. `make test` was not run in either
  arm; a deletion that changes `mixp` by 8 cells would very likely move pins.
* **No CGAL-backend run.** Manifold backend only.
* **`mixp`'s TW** is inferred (TW=1 from the cited invocation, TW=4 the model
  default). Both run, both agree in direction.
* **`rib.scad`'s 18° threshold.** It sets only `$fn`, so its crease threshold is
  pinned at 18° for `$fn >= 30`. Run unmodified to compare with the record; the
  sweep is therefore not a true tessellation sweep of the threshold.
* **Only two passes per arm** on the corpus, one pass on the sweeps. The 16
  nondeterministic models may be undercounted; no conclusion here rests on a
  model in that set.
* `cone_fn192` (156 GB OOM), `cone_fn384`, `sph_across_fn384` — excluded, not in
  the 225. `d21_fn384` and `falseacc384` excluded to keep the 225 like-for-like
  (227 `.scad` present, 225 measured).
