# D17 final adversarial review — `ed540444d` (branch `fix-blockers`)

**Verdict: DO NOT SHIP.**

Review worktree: `/Users/kerem/Devel/openscad/.claude/worktrees/rev-final`, detached at
`ed540444d`, built from a copy of the reviewed build tree with paths rewritten. The
reviewed worktree `agent-af624bee52a8772ef` was not modified.

## Builds and their proof

| build | what | mtime proof |
|---|---|---|
| `fix` | `ed540444d` + review instrumentation, all of it env-gated | rebuilt at each edit, binary mtime read and newer than the source edit every time |
| `head` | clean `944e0cbef`, built in this same tree by checking out `944e0cbef`'s `FilletBuilder.cc` and `FilletBuilder_test.cc` | `git diff --stat 944e0cbef HEAD` shows those two files as the **only** non-`work/` difference, so this is a true HEAD |

**Self-proof that the instrumented build is the shipped build**: with the diagnostics
off it is byte-identical to the reviewed worktree's own untouched binary on
`bcurve`, `opocket`, `rib`, `pocket` under `LOCALGROUP=1`.

**Self-proof of the mesh checker** (`work/ab/mesh2.py`, used for every count below):
a hand-written valid cube reads `nonman=0`, the same cube with one face duplicated
reads `nonman=3`.

Instrumentation added to the review build only, all env-gated and off by default:
`OPENSCAD_FILLET_RDIAG=1` prints, per `buildRoundSolid` call, the candidate/bent/served
vertex census, each candidate vertex's position, chain-end count, single-segment count
and whether a junction exists there, and — after the union-find — `anyServed`, the
chain count, the **group count** and the number of singleton groups.
`OPENSCAD_FILLET_GLOBALSUB=1` forces the pre-feature grouping branch;
`OPENSCAD_FILLET_NOSTRAIGHT=1` disables only the `arrivesStraight` gate.

---

# Blocker — `anyServed` is global, and on a mixed model per-chain grouping leaks to every withheld vertex

This is the earlier review's B1, narrowed rather than fixed. `anyServed` is one flag
for the whole `buildRoundSolid` call. Where a model contains **one** served seam vertex
and **any** withheld ones in the same call, the union-find takes the local branch for
all of them; the withheld vertices emit no junction (they are brushed, so
`chainJunctions` never emits one), so nothing links their chains and every chain
subtracts on its own — layer 1's split with none of layer 2's repair. Every model in
the corpus is all-served or all-withheld, so the corpus cannot see this.

## Reproduction — `work/rev2/mixc.scad` (curved) and `work/rev2/mixp.scad` (planar)

Both are `bcurve.scad`/`bars.scad` — whose seam vertices the rule withholds — with a
walled square pocket standing on the same plate under the same slab brush, whose four
floor corners the rule serves. `MIXED=0` drops the pocket and leaves the withheld
model alone.

```
work/rev2/mixsweep.sh work/rev2/mixc.scad c1 1 16 160 4     # $fn 16..160 step 4
work/rev2/thsweep.sh  work/rev2/mixp.scad p1 1              # theta x r, 15 cells
```

**Census, identical on every row** — `cand=6 bent=2 served=4 njunc=0`:

| | `anyServed` | chains | groups | singleton groups |
|---|---|---|---|---|
| withheld model alone (`MIXED=0`) | 0 | 2 | **1** | 0 |
| mixed model (`MIXED=1`) | 1 | 7 | **7** | 7 |

The two withheld chains that shared group 0 alone are in singleton groups the moment
one unrelated pocket corner elsewhere in the model is served.

**`mixc.scad`, non-manifold edges after welding, `$fn` 16..160 step 4 (35 rows run):**

| | bad tessellations | worst |
|---|---|---|
| HEAD | 17 / 35 | 5 |
| candidate, `LOCALGROUP=1` | **26 / 35** | **52** |
| candidate + `GLOBALSUB=1` | 17 / 35 | 5 — **equal to HEAD on every row** |

Worse than HEAD at 20+ tessellations, and it does not shrink with refinement: 52 bad
edges at `$fn`=140, 40 at 148, 27 at 152, against HEAD's 0, 4, 4. By the owner's own
bar that is a defect, not faceting.

**`mixp.scad`, θ × r, 15 cells:**

| | 60/1 | 60/2 | 60/3 | 75/1 | 75/2 | 75/3 | 90/1 | 90/2 | 90/3 | 105/1 | 105/2 | 105/3 | 120/1 | 120/2 | 120/3 | bad |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| HEAD | 1 | 1 | 1 | 0 | 3 | 1 | 0 | 1 | 0 | 0 | 2 | 0 | 0 | 3 | 1 | 9/15 |
| candidate | 5 | 8 | 18 | 1 | 7 | 5 | 2 | 0 | 3 | 10 | 0 | 1 | 14 | 10 | 3 | **13/15** |
| `GLOBALSUB=1` | 1 | 0 | 1 | 0 | 2 | 1 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 2 | 1 | 6/15 |

Cause pinned, not inferred: forcing the grouping branch back to global — nothing else
changed — returns HEAD's numbers on the curved sweep at all 35 rows and **beats** HEAD
(6/15 against 9/15) on the planar family. So on the mixed model everything the seam
rule does except the grouping is a net win, and the grouping is the whole of the loss.

## And the split buys nothing, on the family it was written for

Forcing the grouping global (`GLOBALSUB=1`) on the models the rule *serves*:

| | shipped | grouping forced global |
|---|---|---|
| `opocket.scad` TW=5, 15 cells | 0 / 15 | **0 / 15** |
| `opocket.scad` TW=0.15, 15 cells | 1 / 15 (θ=75 r=1) | **1 / 15 (θ=75 r=1)** |
| containment, TW=1.0 proud volume | 0.011858 | **0.011858** |

Layer 1 — the per-chain subtraction, `anyServed` and the union-find gating that
carries — changes **no** measured result on the family it exists for, and is the
sole cause of the blocker above. Every win claimed for the feature comes from
layer 2 (skipping the ball at a seam vertex and running the beads past it).

---

# Attack 3 — `arrivesStraight` reproduces exactly, and does not survive the follow-up question

`rib.scad`, `$fn` 16..160 step 4, non-manifold edges (my HEAD build, my binary):

| | bad | worst | which |
|---|---|---|---|
| HEAD | 1 / 37 | 1 | 64 |
| gate on (shipped) | 1 / 37 | 1 | 64 — and byte-identical to HEAD at **all 37** |
| gate deleted (`NOSTRAIGHT=1`) | 9 / 37 | 23 | 28 36 44 56 60 64 80 116 136 |

Identical to the reported table, same nine values. The measurement is solid.

**But the badness it is buying off is the grouping's, not the overrun's.** Delete
the gate *and* force the grouping global — `NOSTRAIGHT=1 GLOBALSUB=1` — and
`rib.scad` is **1 / 37, worst 1** (at `$fn`=28), i.e. as good as HEAD, with no gate
at all. So the cheaper way to keep `rib.scad` clean is the one that also fixes the
blocker: do not split the subtraction. The stated metric diagnosis (the overrun
laying a bead sliver outside the wall it was tangent to) is not what the sweep
shows: with the same overrun and global subtraction the sliver does not appear.

**It is worse than that: the gate is throwing away the biggest win in the corpus.**
With the rule ungated and the subtraction global, on the two curved models the gate
withholds:

| `$fn` 16..160 step 4 | HEAD | shipped | ungated + global |
|---|---|---|---|
| `bcurve.scad` | 20 / 37, worst 5 | 20 / 37 (byte-identical to HEAD) | **0 / 37** |
| `mixc.scad` | 17 / 35, worst 5 | 26 / 35, worst 52 | **0 / 37** |
| `mixp.scad`, 15 cells | 9 / 15 | 13 / 15, worst 18 | **0 / 15** |

Sanity-checked, not just counted: at `$fn`=100 `bcurve` reads tris 5830/5830/5846
and volume 21758.996070 / 21758.996070 / 21757.913365 for HEAD / shipped / ungated
+ global, so the 4 → 0 is a real repair of a real mesh, not an empty or inert one.

# Attack 4 — `cs.size() > 1`: the condition is sound, its recorded justification is false

The record states "measured: no served vertex anywhere has fewer than two chains".
That is wrong. `clip.scad`, `gap.scad` and `corner.scad` each have **three** served
vertices of which **two carry exactly one chain end** (e.g. `clip` v=2 at (0,20,0)
and v=4 at (20,0,0), `ends=1 oneseg=1 served=1`). The condition does exclude them;
it changes nothing there only because a third vertex with two ends sets the flag
anyway. `work/rev2/lone.scad` makes the one-end vertices the *only* served ones:
`served=2 anyServed=0`, and the output is byte-identical to HEAD. Note the overrun
is **not** gated by `anyServed` and does fire at those vertices; it happens to be
inert. The condition is right; its evidence in the record is not.

# Attack 5 — the one-segment exemption on a curved wall IS reachable; both stated reasons are wrong on the second count

- **"A brush masks a chain, it does not split one" — CONFIRMED.** `facetseam.scad`
  reports `chains=1 ends=0 cand=0`, and `gap.scad`, whose brush leaves two windows
  in the middle of each bottom edge, still reports `chains=2 ends=3`: two creases,
  not four fragments.
- **"A chain end on a curved wall lands on a facet corner" — FALSE.** In
  `bcurve.scad` itself the ends are at (7.4130, ±3, 5) on an `$fn`=64 R=8 ring:
  azimuth 22.02°, i.e. 3.91 facets — strictly *inside* a facet. It reads bent only
  because the *previous* vertex is a facet corner.
- **The exemption is reachable.** `work/rev2/facet4.scad` (FN=12, R=60, so facet
  seams are 30° under a 45° threshold and the wall reads curved; two ribs inside
  one facet with a 6 mm gap) gives `thr=45 cand=4 bent=2 served=2`, with both
  served vertices carrying a **single-segment** arrival on a curved wall. So the
  gate is passed on a curved wall — the hole is real, not hypothetical.
- **It is harmless, and now for a reason rather than for want of an exploit.** The
  output is byte-identical to HEAD at `SEAMOVER` = default, 0, 0.5 and 2. A
  single-segment arrival lies inside one flat facet, so extrapolating it stays in
  that facet's plane; the sliver defect needs the extrapolation to leave the
  surface, which needs the arrival to be a chord of a curve — necessarily
  multi-segment. And the only direction the overrun can run past such a vertex is
  into the very feature whose unfilleted crease made it a seam vertex. That is a
  general argument, and it closes the question the other way from how it was left.

# Attack 6 — the rest of the claims, checked

| claim | result |
|---|---|
| HEAD reference genuinely `944e0cbef` | **VERIFIED** — `git diff --stat 944e0cbef HEAD` = those two files only; my independently built HEAD gives byte-identical exports to their `headbin` on 8 models |
| `bcurve.scad` `cmp`-equal at all 37 tessellations | **VERIFIED** — 37/37 IDENT, HEAD 20/37 worst 5 = candidate, `served=0` on every row (correctly labelled inertness) |
| `bars.scad` byte-identical in all 15 θ×r cells | **VERIFIED** — 15/15 IDENT, 7/15 bad on both |
| 13/13 controls byte-identical, env unset | **VERIFIED** (ctrl1–5, boss, grid, rib, ribfa, pocket, rhomb, slab, corner, grid100, multi, opocket, opocket2, clip, gap, bars — all IDENT; `oblq.scad` exports nothing on either binary) |
| `rhomb` 1→0, `slab` 1→0, nothing worse under `LOCALGROUP=1` | **VERIFIED** on the whole corpus |
| suite 1688 assertions / 83 cases, all envs | **VERIFIED** in six: both unset, `LOCALGROUP=1`, `SEAMOVER=0`, `SEAMOVER=0.5`, `LOCALGROUP=1`+`SEAMOVER=0`, `LOCALGROUP=1`+`SEAMOVER=0.5`. Built from pristine sources (no `RDIAG` string in the test binary, object newer than the revert) |
| no pin changed; whole test diff is the fixture | **VERIFIED** by reading `git diff fd1178e74..HEAD -- src/…_test.cc` |
| 15-cell oblique TW=5 → 0/15 vs HEAD 6/15 | **VERIFIED** exactly |
| 15-cell oblique TW=0.15 → 1/15 (θ=75 r=1) vs HEAD 7/15 | **VERIFIED** exactly |
| containment TW 5/1.55/1.5/1.4/1.0/0.15 | **VERIFIED**: 0/0/0/0/0.011858/2.430507 vs HEAD 0/0/0/0.000093/0.036172/1.913997 |
| ideals: corner 0.00061, cube-top 0.01791, pocket missing 0.04893 | **VERIFIED**, all three |
| brush fidelity `clip.scad` at exactly BX | **VERIFIED** on 6 of 12 cells (BX 5/10/18 × r 1/3), all +0.00000 |
| `gap.scad` 4.00000 / 8.00000 / 12.00000 | **NOT CHECKED** — no time; the mechanism is the same one `clip` exercises |
| timing | **NOT RESOLVABLE** — load average was 54–58 throughout (other agents). Paired interleaved grid100: HEAD 8.28/9.09/8.13 s, `LOCALGROUP=1` 8.70/10.03/7.48 s. No regression signal, no usable resolution |

# Maintainability

Four interacting mechanisms is not the shape this feature wants, and the measurements
say two of them can go: the per-chain grouping and its `anyServed` guard change no
measured result on the family they were written for, cause the blocker, and are the
reason `arrivesStraight` had to be invented; with the subtraction left global, the
gate is unnecessary on `rib.scad` and actively harmful on `bcurve.scad`. What is
left — skip the ball at a seam vertex, run the beads past it, bound by `seamRoom` —
is one idea with one knob, is the part that carries every win in the corpus, and is
the version I would ship after re-running the suite and the controls against it.
