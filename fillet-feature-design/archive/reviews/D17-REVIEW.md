# D17 adversarial review — `fd1178e74` (`arrivesStraight`), branch `fix-blockers`

**Verdict: DO NOT SHIP.**

Review worktree: `/Users/kerem/Devel/openscad/.claude/worktrees/rev-d17` (detached at
`fd1178e74`), built from a copy of the reviewed build tree with paths rewritten.
The reviewed worktree `agent-af624bee52a8772ef` was not modified; its own binary was
run read-only to confirm the headline blocker.

Binaries used, mtimes checked against the last source edit at every rebuild:

| build | path | note |
|---|---|---|
| `head` | `…/a47a8762-…/scratchpad/wt-head/build/…/OpenSCAD` | clean `944e0cbef` |
| `fix` | `…/rev-d17/build/…/OpenSCAD` | `fd1178e74` + review instrumentation, env-gated |
| `their` | `…/agent-af624bee52a8772ef/build/…/OpenSCAD` | untouched, read-only |

## Instrumentation and what it can see

`OPENSCAD_FILLET_SEAMDIAG=1` prints, per `buildRoundSolid` call: the number of open
chain-end vertices; how many satisfy `creaseLeavesUnfilleted` (= candidate seam
vertices, the layer-1 predicate before the gate); how many of those are in
`arrivesBent`; and how many are therefore *served* by the rule. It calls the shipped
predicates themselves, so it cannot drift from them. Per candidate vertex it also
prints how many arriving chains are single-segment, and per chain end the raw
direction cosine the gate thresholds.

Self-proving invariants run before use: the census is unchanged under a rotation of
exactly one facet (5.625° at `$fn`=64) and under uniform scaling over five decades
(S = 0.01 … 1000). Both come back identical, so the metric is not reading an
artefact of orientation or units.

Two extra env gates were added **to the review build only**, to isolate cause:
`OPENSCAD_FILLET_GLOBALSUB=1` forces the pre-layer-1 global subtraction while leaving
everything else on, and `OPENSCAD_FILLET_NOSTRAIGHT=1` disables the `arrivesBent`
filter to reproduce pre-gate behaviour.

---

# Blockers

## B1 — where the gate withholds the seam rule, layer 1 stays on, and the result is much worse than HEAD

The commit message and the code comment both state that a curved arrival is
"handed back to the seated ball … i.e. back to HEAD's behaviour, which the owner's
framing already names as the safe fallback", and that the gate "gates the whole rule,
not only the overrun". **It does not.** `arrivesStraight` feeds `seamVertex`, which
gates only three things: the corner-cell/junction skip, the ball skip, and the
overrun. The union-find that decides **global vs per-chain canal subtraction** is
gated on `localGroup` alone:

```
if (localGroup) {                       // ← not seamVertex, not arrivesStraight
  for (const Junction& j : junctions) { if (seamVertex(j.vert)) continue; … }
} else {
  for (i…) parent[findRoot(i)] = findRoot(0);   // one group: HEAD
}
```

Chains are linked *only* at junctions. Where a model produces **no junctions at all**
— which is exactly what happens at a brushed seam vertex, because the brush pass puts
it in `noCorner` and `chainJunctions` never emits one — every chain lands in its own
group and the subtraction silently goes per-chain, whether or not any seam vertex
survives the gate. `rib.scad` escapes only by accident: there the creases are refused
for *size*, not by the brush, so junctions do exist (`njunc=2`), the gate makes them
non-seam again, the chains re-link into one group, and the output is byte-identical to
HEAD. Change the reason the crease is unfilleted from *size* to *brush* and the escape
hatch closes.

### Reproduction — `work/rev/bcurve.scad`

`rib.scad`'s own geometry with the rib made 8 mm tall, so the two vertical
rib/cylinder creases fit `r` perfectly and are left unfilleted by a `SLAB`=0.5 slab
brush — the exact construction `opocket.scad` uses, on a curved wall. The two vertices
where the rib meets the boss at plate level are genuine **brushed** seam vertices and
the crease arriving at them is the tessellated base arc.

```
OPENSCAD_FILLET_LOCALGROUP=1 OpenSCAD -o out.off -D FN=80 work/rev/bcurve.scad
```

Diagnosis at `$fn`=80: `cand=2 bent=2 served=0 njunc=0`; arriving cosine
0.99691733 (the 4.5° facet turn) against the gate's 1 − 1e-9.

Non-manifold edges after welding, `$fn` 16..160 step 4, 37 tessellations:

| build | bad tessellations | worst count |
|---|---|---|
| HEAD | 20 / 37 | 5 edges |
| candidate, `LOCALGROUP=1` | **28 / 37** | **56 edges** |
| candidate + `GLOBALSUB=1` | matches HEAD exactly | |

Selected rows (`head` / `fix`): `$fn`=48 → 0 / 12; 64 → 0 / 15; 80 → 0 / 25;
100 → 4 / 50; 140 → 0 / 52; 156 → 3 / 56.

It does **not** shrink with refinement — 56 bad edges at `$fn`=156 — so by the
owner's own bar this is a defect and not faceting. `SEAMOVER=0` gives byte-identical
numbers to the default, so the overrun is not involved: this is layer 1 alone.
Reproduced independently on the reviewed worktree's own untouched binary
(15 and 25 non-manifold edges at `$fn` 64 and 80, against HEAD's 0).

Cause pinned, not inferred: with `GLOBALSUB=1` the same binary returns
`vol=21757.862161, tris=4022, nonman=0` at `$fn`=80 — HEAD's numbers to the last
digit. The sole difference is the grouping branch.

### The same fault on a planar model — `work/ab/bars.scad`

The gate blocks on any crease that *bends* at the vertex, not only on curved walls.
`bars.scad` (the two-bars family from the brief's constraint 3, entirely planar) has
4-segment chains meeting at 90°: `cand=2 bent=2 served=0 njunc=0`. Swept over the same
θ × r grid as the oblique table:

| | 60/1 | 60/2 | 60/3 | 75/1 | 75/2 | 75/3 | 90/1 | 90/2 | 90/3 | 105/1 | 105/2 | 105/3 | 120/1 | 120/2 | 120/3 | bad cells |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| HEAD | 1 | 0 | 1 | 0 | 1 | 1 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 2 | 1 | **7 / 15** |
| candidate | 5 | 9 | 18 | 1 | 6 | 5 | 2 | 0 | 3 | 9 | 0 | 1 | 5 | 10 | 3 | **13 / 15** |

`GLOBALSUB=1` at θ=60/r=3, 105/r=1, 120/r=2 returns 1, 0, 2 — HEAD's values —
against the candidate's 18, 9, 10. Same root cause.

This also falsifies a reported number: the commit's "Nothing else moved" table lists
`bars` at 0 bad edges for HEAD and candidate alike. That is true only of the single
default cell θ=90/r=2; across the family it is 7/15 against 13/15.

**What would fix it:** either gate the union-find on `seamVertex` too (link every
junction *and* every non-seam chain-end vertex, so a model with no seam vertices
groups exactly as HEAD does), or hold local grouping back until it is needed. Either
way the invariant to assert is the one the commit already claims: *with zero served
seam vertices, `LOCALGROUP=1` must be byte-identical to HEAD.* It currently is not.

## B2 — the suite is still not hermetic against `OPENSCAD_FILLET_SEAMOVER`

`FilletBuilder_test.cc`'s `SeamRule` fixture saves, sets and restores
`OPENSCAD_FILLET_LOCALGROUP` and nothing else. `OPENSCAD_FILLET_SEAMOVER` is read
straight out of the caller's environment. This is the exact failure the brief records
as having been fixed:

```
env -u OPENSCAD_FILLET_LOCALGROUP -u OPENSCAD_FILLET_SEAMOVER ./OpenSCADUnitTests
  → All tests passed (1688 assertions in 83 test cases)
OPENSCAD_FILLET_LOCALGROUP=1 ./OpenSCADUnitTests
  → All tests passed (1688 assertions in 83 test cases)
OPENSCAD_FILLET_SEAMOVER=0   ./OpenSCADUnitTests
  → 83 cases | 82 passed | 1 failed ; 1688 assertions | 1687 passed | 1 failed
OPENSCAD_FILLET_SEAMOVER=0.5 ./OpenSCADUnitTests
  → 83 cases | 82 passed | 1 failed ; 1688 assertions | 1686 passed | 2 failed
```

A developer with the variable exported in their shell gets a red suite for no reason.
Fix is one line: extend `SeamRule` to save/set/restore `SEAMOVER` as well.

---

# Attack 1 — where the rule still fires, as a number

Census over 23 models (the twelve controls, the whole of `work/ab`, and four curved
models written for this review). Columns: candidate seam vertices; vertices actually
served after the gate; served vertices at which **at least one** arriving chain is a
single segment.

| model | cand | served | served with a 1-segment arrival |
|---|---|---|---|
| opocket, opocket2, pocket, rhomb, slab, multi, oblq | 4 | 4 | 4 |
| corner, clip, gap | 3 | 3 | 3 |
| xbar (cross-drilled bar) | 8 | 8 | 8 |
| ctrl1–5, boss, grid, dboss | 0 | 0 | 0 |
| **rib, ribfa, bcurve, bars** | **2** | **0** | 0 |

**The numbers.**

- On **curved** models the rule fires at **0 of 6** candidate seam vertices — 0/2 on
  `rib.scad`, 0/2 on `ribfa.scad` (re-measured through `$fa=360/$fn`, effective
  threshold 6.75° at `$fn`=80, so it can see its own tessellation), 0/2 on
  `bcurve.scad`. Every curved arrival is blocked, at every `$fn` from 16 to 160.
- Across the whole corpus there are **45 served vertices**. **45 of 45** have at least
  one single-segment arriving chain and **43 of 45** have *only* single-segment
  arrivals. The collinearity test proper fires at exactly **2** chain-ends in the whole
  corpus, both in `multi.scad`, both at cosine **exactly 1.0** — a straight planar
  crease carrying intermediate stations. Of the 26 multi-segment chain-ends measured,
  2 read cosine exactly 1, **0 fall anywhere inside the tolerance band
  (1 − 1e-9, 1)**, and 24 fall below it (0.995 on a tessellated arc, 0 on a right-angle
  turn). The 1e-9 tolerance therefore never distinguishes anything: the predicate as
  shipped is "exactly collinear, or one segment".
- On a curved model with a genuine brushed seam vertex, the rule does **not** fire,
  and the output is **not** HEAD's — it is 28/37 tessellations non-manifold against
  HEAD's 20/37, with up to 56 bad edges against HEAD's 5. See B1.

**Judgement against "generalisation over patchwork."** As shipped this is not a
general rule with a safety gate; it is a planar-single-segment rule. Worse, the
discontinuity is not benign: on one side of it you get layer 1 + layer 2 (good), on
the other you get layer 1 alone (worse than HEAD). `arrivesStraight` is a
discontinuity dressed as a principle. The *stated* principle is sound and the physics
behind it is right (see attack 3); what is wrong is that it was applied as a
predicate on the chain rather than as a reason to fall all the way back.

---

# Attack 2 — the 1e-9 tolerance and the one-segment hole

**Scale.** The test is `a.dot(b) / (‖a‖‖b‖) > 1 − 1e-9` — a pure direction cosine,
dimensionless, so it is scale-free by construction, and measured so: `work/rev/oscale.scad`
(opocket with every length × S) gives `cand=4 bent=0 served=4` identically at
S = 0.01, 0.1, 1, 100, 1000. The one absolute constant, the `‖a‖ < 1e-12` degenerate
guard, is five orders below anything a 0.1 mm model produces. **No blocker here** —
this was worth worrying about and is clean. The residual exposure is catastrophic
cancellation in `end − mid` for a model built far from the origin relative to its
feature size (features of 1e-3 at coordinates of 1e6); not reachable in any model
in the corpus and not worth code.

**Rotation.** Identical census at 0°, 5.625° (exactly one facet), 17°, 90°.

**The one-segment exemption is not a corner case — it is nearly the whole mechanism.**
As shown above, 45/45 firings have a single-segment arrival and 43/45 have nothing
else; and since no cosine anywhere in the corpus lands inside the tolerance band, the
1e-9 constant is doing no work at all — it could be 1e-3 or 1e-15 with identical
results on every model measured. A constant that no measurement can distinguish from
its neighbours is a constant nobody can maintain. The comment justifying it ("a crease with no
station between its ends is a straight line") is true of the crease but says nothing
about the *wall* the bead rides, which is what the chord fault is actually about. So
the hole is real in principle. I could not build a model that walks through it: on
every curved wall I could construct, a crease long enough to matter is tessellated
into ≥ 3 stations, and a crease short enough to be one segment is refused for size
before it reaches the rule. I record this as **an unclosed hole with no demonstrated
exploit**, not as a blocker — but it means the gate's protection is incidental to how
OpenSCAD happens to tessellate, not derived from anything.

---

# Attack 3 — the chord mechanism

Verified independently, with the gate disabled (`NOSTRAIGHT=1`) so the pre-gate
behaviour is reproducible. `rib.scad` at `$fn`=80, one non-manifold edge with **four**
faces on it:

```
(7.42505 2.96927 6.93825) — (7.43088 2.97129 6.99195)
radius 7.996745 → 8.002908 on a cylinder of radius 8, z ≈ 6.94–6.99
facet sagitta at R=8, $fn=80: 0.006168
```

Exactly the reported edge and the reported radii. The claim's load-bearing part — that
the produced chord leaves the circle **on the outside** — holds: the far end is at
radius 8.0029, i.e. 0.0029 *outside* the wall, against a sagitta of 0.0062. Sign and
order of magnitude both confirm the mechanism. Four faces on the edge is two surfaces
lying on each other, as claimed.

**But the predicted `$fn` scaling does not hold, and should not have been asserted.**
At `$fn`=40 and `$fn`=160 with the gate disabled there is *no* bad edge at all
(0 non-manifold edges), so the defect does not shrink smoothly with the sagitta — it
appears and disappears with `$fn` (9 of 37 tessellations pre-gate). It is a boolean
robustness threshold that the sagitta pushes the geometry across, not a defect whose
size is a function of the sagitta. **The pin is right and the stated derivation is
over-claimed** — reported separately as the brief asks. This does not change the
conclusion that a bent arrival should not be extrapolated.

---

# Attack 4 — the reported numbers, re-measured

| claim | re-measured | verdict |
|---|---|---|
| `rib.scad` `$fn` 16..160 step 4: HEAD 1/37 (at 64), candidate 1/37 (at 64) | HEAD 1/37 at `$fn`=64, candidate 1/37 at `$fn`=64, same edge count | **confirmed** |
| candidate on `rib.scad` reverts to HEAD | byte-identical STL to clean `944e0cbef` under `LOCALGROUP=1` | **confirmed — and it is the whole of the win** |
| `SEAMOVER=0` on `rib.scad` = 10/37 | now 1/37, because the gate makes the rule inert on `rib` at every `$fn`; the 10/37 column is a stale pre-gate figure | stale, not wrong |
| 15-cell oblique, TW=5: HEAD 6/15 → candidate 0/15 | HEAD 6/15, candidate 0/15 | **confirmed** |
| 15-cell oblique, TW=0.15: HEAD 7/15 → candidate 1/15 at θ=75/r=1 | HEAD 7/15, candidate 1/15, at θ=75/r=1 | **confirmed** |
| `bars` "nothing moved", 0 vs 0 | true at the default cell only; 7/15 vs 13/15 across θ × r | **false as stated (see B1)** |
| `clip.scad` 12/12 cells at +0.00000 | 12/12 at +0.00000 over BX ∈ {5,10,15,18} × r ∈ {1,2,3} | **confirmed** |
| `gap.scad` exact at 4 / 8 / 12 | sharp edge resumes at exactly 4.00000, 8.00000, 12.00000 | **confirmed** |
| Blocker A containment TW=1.0: 0.011858 vs HEAD 0.036172 | not re-measured (time) | unchecked |
| ideals 0.00061 / 0.01791 / 0.04893 | not re-measured (time) | unchecked |

My `clip`/`gap` metric was wrong on the first pass — it read the far end of the
*cube* edge, which is at x=20 whatever the brush says. The figures above come from the
corrected metric (the x at which the *sharp* bottom edge resumes, i.e. the first
vertex with y=z=0 and x>0), which is the quantity the brush actually sets.

---

# Attack 5 — controls and suite

- **Byte-identical to clean `944e0cbef`**, `cmp` on exported STL, both binaries with
  both variables unset: `ctrl1 ctrl2 ctrl3 ctrl4 ctrl5 boss grid rib pocket rhomb slab
  corner grid100` — **13/13 BYTE-IDENTICAL**. Confirmed.
- **Suite**: `1688 assertions in 83 test cases, all passed` with the environment unset
  and with `LOCALGROUP=1`. Confirmed. Not hermetic against `SEAMOVER` — see B2.
- The reference volumes 19468.9342 / 20489.7290 / 906.7936 do not correspond to
  `ctrl1`/`ctrl2`/`ctrl3` on **either** binary (ctrl1 reads 9512.5968 on both). The
  implementers' reading that the literals are stale is right, and byte-identity
  against a same-day HEAD build is the stronger control anyway. No blocker.
- Known nondeterminism (hole-in-a-flat-plate vertex order, `rounded_cube`) not
  reported as regressions.

---

# Attack 6 — residuals

- The accepted residual (θ=75 / r=1 / TW=0.15, one edge) reproduces exactly, and the
  reasoning holds: `opocket.scad` is entirely planar, its chains are single-segment,
  the census shows `bent=0` at all four corners, so the gate provably cannot be
  touching it. **The stated reasoning is sound.**
- **It is not the only residual.** Sweeping `$fn` on a genuinely *tessellated* model —
  the check a prior report could not make because `pocket.scad` and `rhomb.scad` are
  planar — turns up B1: 28/37 tessellations bad on `bcurve.scad` against HEAD's
  20/37, worsening rather than shrinking with refinement. And `bars.scad`, planar but
  bent, carries a second one at 13/15 cells against HEAD's 7/15.

---

# Attack 7 — env hygiene and dead code

**Clean:**
- `OPENSCAD_FILLET_SEAMOVER` parses through `std::istringstream` imbued with
  `std::locale::classic()`, not `atof`/`strtod`. Verified behaviourally on this
  comma-decimal machine: `abc`, `0,1`, `3.0` and `-1` are each refused with exactly
  **one** warning and fall back to 0.10. (Note the diagnostics themselves print
  comma decimals — trap 6 is live in this build's own `printf` output.)
- `SEAMOVER=0` is a true control: the ball suppression and the junction skip are
  gated on `seamVertex` only and explicitly documented as not gated on the overrun.
  Code and measurement agree — `SEAMOVER=0` and the default differ only in the
  overrun, and on `bcurve` they are identical because nothing is served.

**Minor, not blocking:**
- `LOCALGROUP` is a presence test, so `OPENSCAD_FILLET_LOCALGROUP=0` turns the
  feature **on** (measured: `served=4` on `pocket.scad`). Surprising for a knob whose
  documented form is `=1`.
- `FilletBuilder.cc:2906`, `const int gid = cit == chainsAt.end() ? 0 : …` — the
  fallback to group 0 is still unreachable: a `Junction` exists only at a vertex a
  chain end lands on, so `chainsAt.find(j.vert)` cannot be `end()`. A prior review
  found this; it has not been removed. If it ever *were* reachable it would put a
  corner ball in the wrong group, so it should be an assert, not a silent 0.

---

# Summary of what was verified clean

Scale and rotation invariance of the gate; the chord mechanism and the exact defect
edge; the `rib.scad` 37-value sweep; both 15-cell oblique tables; brush fidelity on
`clip` and `gap`; 13/13 byte-identical controls; 1688/83 under unset and under
`LOCALGROUP=1`; `SEAMOVER` parsing and its status as a true control; the accepted
residual's reasoning.

# Summary of what blocks

1. **B1** — with the gate withholding the rule, layer-1 grouping still fires and the
   output is far worse than HEAD, on curved models (`bcurve.scad`, 28/37 vs 20/37,
   56 bad edges vs 5) and on planar models whose creases bend (`bars.scad`, 13/15
   cells vs 7/15). The commit's claim that a bent arrival falls back to HEAD is false.
2. **B2** — the suite is not hermetic against `OPENSCAD_FILLET_SEAMOVER`
   (`=0` → 82/1, `=0.5` → 82/1 and two failed assertions).
