# D21 — the false-negative side of `blindFloor = 1e-4 * max(1.0, size)`

Adversarial review of `473188bad` on `worktree-agent-ad442fb5e2bfb0c44`.
Narrow scope: **can a genuine blind miss fall *below* the floor**, so the gate
says "fits" when it does not and the D21 shards come back silently. Every
measurement so far had tested that the floor is high enough to stop false
*refusals*; nobody had tested the other side.

**Verdict: SHIP, with one required follow-up.** A genuine miss below the floor
does exist and I produced it. It does not shatter anything — forcing its
refusal changes the output by exactly zero. The regime that *does* shatter is
sub-millimetre `size`, and its cause is not `1e-4` but the `max(1.0, size)`
clamp, which is causally confirmed and one line to fix.

285 runs, all checkpointed. Raw rows:
`…/scratchpad/m/{A,S,S2,T,R,F,F192,K,N,P}.jsonl`.

---

## Method, and why each number can see what it claims to

- **Binary A (reference).** The reviewed worktree's own
  `build/OpenSCAD.app/Contents/MacOS/OpenSCAD`, mtime `03 Aug 01:26` against
  sources at `01:25` on a clean tree at `473188bad` — verified current, then
  copied to scratchpad so a concurrent rebuild elsewhere could not swap it
  underneath the survey. The reviewed worktree was never modified.
- **Binary B (instrumented).** Built in **my own** worktree
  `…/worktrees/margin-d21` from `473188bad` plus one reviewer-only change: the
  floor constant and the `max(1, ·)` clamp become readable from the environment
  (`OPENSCAD_FILLET_BLINDFLOOR`, `OPENSCAD_FILLET_BLINDFLOOR_NOCLAMP`), parsed
  through an `istringstream` imbued with `std::locale::classic()` (trap 6).
  Binary mtime `10:31`, last source edit `10:11` — checked, not assumed.
  It reproduces binary A exactly at the default `k = 1e-4`
  (D21 `$fn` 48: 101 blind, 87 refused, volume 18898.0673, genus 0), which is
  the behavioural proof that the knob is inert at its default and that both
  binaries are the repaired build rather than the `1e-9` one.
- **Refusals are counted, never inferred** (trap 5). The commit already emits
  `OPENSCAD_FILLET_SIZEGATE_DEBUG=1`, one line per crease carrying `tested=`,
  `exempt=`, `maxOffFace_exempt=` and `verdict=`. Every run below reports
  `blind` (creases with `tested=0, exempt>0`), `blindRef` (those the fallback
  refused) and **`PASSED_NZ`** — blind creases with a strictly non-zero miss
  that the floor let through. `PASSED_NZ` is the false-acceptance detector, and
  it is read from the gate itself, not from the mesh.
- **Genus from the mesh.** OFF export, welded at `1e-8 × bbox diagonal`, χ and
  component count computed directly, plus edges used by ≠2 faces. OpenSCAD's
  own `Genus:` line recorded alongside. The two agree in direction everywhere;
  where the integers differ it is the component count, and per trap 4 the
  quoted `genus` below is OpenSCAD's with the mesh figures in the raw rows.
- Every model sets `$fa = 360/$fn; $fs = 0.001;` (trap 3).
- The gate does something in every headline run, so nothing here is inertness
  (trap 2). Baseline reproduction of the repairer's own numbers:

| $fn | genus new | genus legacy | vol new | vol legacy | blind | blindRef |
|-----|-----------|--------------|---------|------------|-------|----------|
| 24  | 0 | defective | 18723.1146 | 18446.3245 | 43 | 39 |
| 48  | 0 | −15 | 18898.0673 | 18533.0006 | 101 | 87 |
| 96  | 0 | −42 | 18942.5793 | 18436.0676 | 252 | 245 |
| 192 | 1 | −22 | 18698.72 | — | 579 | 575 |

Refusal counts and volumes match the record to the digits quoted.

---

## 1. A genuine blind miss below the floor — found, and inert

**Configuration** (unit scale, at the origin, nothing extreme):

```scad
$fn = 192; $fa = 360/192; $fs = 0.001;
module cp() { union() { cylinder(h = 60, r = 10);
  translate([0, 0, 30]) rotate([-75, 0, 0]) cylinder(h = 25, r = 2); } }
fillet(r = 4) cp();
```

D21 family with **`Rb = 2`, `tilt = −75`, `r = 4`, `$fn = 192`** — one tilt step
and one branch radius from the case the repair was justified on.

What the gate saw: 483 blind creases, 481 refused, and **2 passed carrying a
miss of `1.832e-04` mm** against a floor of `1e-4 × max(1,4) = 4.00e-04`. They
sit **2.2× below the floor**. The **smallest refused** miss in the same run is
`4.588e-04` — **1.15× above it**.

So at `$fn` 192 the floor is not sitting 1.9× under the evidence. It is sitting
**inside** the evidence, with genuine misses on both sides. The 1.9× figure was
an artefact of measuring one shape, at one radius, at one `$fn`.

`1.832e-04` is not arithmetic: the repairer's worst measured boundary noise is
`1.71e-07` on coordinates of order 40, and these coordinates are also of order
40. The passed miss is **~1070× the measured noise ceiling**.

**But it is inert.** Forcing the refusal with the instrumented binary:

| floor `k` | floor | blind | refused | PASSED_NZ | genus | volume |
|-----------|-------|-------|---------|-----------|-------|--------|
| 1e-4 (shipping) | 4.0e-4 | 483 | 481 | 2 | −1 | 18698.72 |
| 1e-5 | 4.0e-5 | 483 | **483** | 0 | −1 | 18698.72 |
| 1e-6 | 4.0e-6 | 483 | **483** | 0 | −1 | 18698.72 |

Identical volume, identical genus. The two sub-floor creases contribute nothing
downstream. The configuration *is* genus −1 (legacy is −39, so the fix repairs
most of it), but that residual is **not** caused by the false acceptance. Two
further defective configurations behave the same way: `$fn` 96 `tilt −70
Rb 2 r 3` (genus −3) and `$fn` 192 `tilt −60 Rb 2 r 4` (genus −1) are unchanged
by dropping the floor 100×.

**Reachability.** It needs no extreme ratio: unit scale, at the origin, a 4 mm
radius on a 60 mm part, coordinate-to-radius ratio ~15. What it needs is
`$fn` 192. It is the *only* configuration at ordinary scale, out of 285 runs,
where the floor passed a blind crease carrying a non-zero miss; every other
such case in the corpus is a sub-millimetre-scale run from §3.

**Reading:** the brief's literal blocker criterion ("if any real miss lands
under the floor, that is a blocker") is met. The harm it stands proxy for is
not. I could not construct a case at ordinary scale where the floor's height
costs a valid solid. The residual defects that do exist at `$fn` 192 —
`tilt −60 Rb 3 r 1` (genus −5, 55 blind creases, **all 55 refused**),
`tilt −75 Rb 2 r 0.5` (genus −1, **no blind creases at all**) — cannot be
about this floor, because the gate refused everything it saw or saw nothing.

## 2. The whole family — where the margin actually is

`Rb ∈ {1,2,3,5,8} × tilt ∈ {45,50,55,60,65,70,75,80,84,89,90}` at `r = 4`, at
`$fn` 48 and 96 (110 runs); `$fn` 192 on the tight-margin configurations;
`r ∈ {8,6,5,4,3,2,1,0.5,0.25,0.1,0.05}` on several `(tilt, Rb)`.

Smallest **refused** blind miss anywhere, per `$fn`:

| $fn | smallest refused | fraction of `size` | headroom over floor |
|-----|------------------|--------------------|---------------------|
| 24  | 1.443e-01 | 3.6e-02 | 360× |
| 48  | 4.863e-03 | 1.2e-03 | 12× |
| 96  | 5.297e-04 | 1.3e-04 | **1.32×** |
| 192 | 4.588e-04 | 1.1e-04 | **1.15×**, with genuine misses below it |

**The trend is the finding.** The smallest genuine blind miss falls by roughly
an order of magnitude per doubling of `$fn`, because the runout lip fragments
into ever shorter chains whose exempt samples sit ever closer to their walls.
The floor is a constant. They were always going to cross, and at `$fn` 192 they
have. `1e-4` has about **one `$fn` doubling of headroom left**.

At `$fn` 24, 48 and 96, across all 110 family runs plus the radius sweeps, the
floor never once passed a blind crease with a non-zero miss.

Two defects at small radii — `$fn` 96 `r = 0.5` (genus −2) and `$fn` 48
`r = 0.25` (genus 2) on the canonical shape — are **identical under `legacy`**,
so they predate this change and are out of scope.

## 3. Scale — where the margin closes, and why it is the clamp

For `size ≥ 1` the decision is **exactly scale-invariant**: evidence and floor
scale together. Canonical D21, `$fn` 48, default vs `legacy`:

| scale | radius | genus new | genus legacy | blindRef | volume |
|-------|--------|-----------|--------------|----------|--------|
| 1e3 | 4000 | 0 | −15 | 87 | 1.88981e13 |
| 1e2 | 400 | 0 | −15 | 87 | 1.88981e10 |
| 10 | 40 | 0 | −14 | 87 | 1.88981e7 |
| 1 | 4 | 0 | −15 | 87 | 18898.07 |
| 0.25 | 1 | 0 | −15 | 87 | 295.282 |
| 0.125 | 0.5 | 0 | −15 | 87 | 36.9103 |
| 0.05 | 0.2 | 0 | −15 | 87 | 2.36226 |
| 0.01 | 0.04 | 0 | −15 | 87 | 0.0188981 |
| 1e-3 | 4e-3 | 0 | −15 | **78** | 1.88981e-5 |
| 1e-4 | 4e-4 | 0 | −15 | **42** | 1.88981e-8 |
| 1e-5 | 4e-5 | **9** | −15 | **20** | 1.88933e-11 |
| 1e-6 | 4e-6 | **−15** | −15 | **0** | 1.8533e-14 |

Scaling **up** is free — identical decisions at 1000×, volumes exact.
Scaling **down** collapses the gate, and the culprit is the clamp: once
`size < 1` the floor stops being a fraction of the size and becomes a fixed
**1e-4 mm**, whose size-relative height rises as `1e-4/size`. Refusals bleed
away from `s = 1e-3`, the solid degrades at `s = 1e-5`, and at `s = 1e-6` the
gate is **completely blind — zero blind refusals — and the output is the legacy
shatter, genus −15, byte-for-byte the pre-fix mesh**. At `$fn` 96 the same
collapse runs 0 → 0 → 50 → −43 over the same four decades.

**Causally confirmed.** Same runs, floor left at `1e-4` but the clamp removed
(`floor = 1e-4 · size`):

| scale | clamped: refused / genus / volume | no clamp: refused / genus / volume |
|-------|-----------------------------------|------------------------------------|
| 1e-3 | 78 / 0 / 1.8898067e-05 | 87 / 0 / 1.8898067e-05 |
| 1e-5 | 20 / **9** / 1.8893283e-11 | 87 / **0** / 1.8898067e-11 |
| 1e-6 | 0 / **−15** | 89 / **0** / 1.8898067e-14 |

Removing the clamp restores exact scale invariance: the volume becomes
`18898.067 × s³` at every scale, and the shatter disappears.

`work/d21-clamp-collapse.scad` is that case as a runnable model. Re-run against
the reviewed binary from the committed file: **104 blind creases, zero refused,
OpenSCAD genus −15, and the mesh is in 19 separate components** — not a
marginal defect but the D21 shards, emitted with no size warning at all.
`work/d21-false-acceptance.scad` is §1: genus −1, one edge shared by more than
two faces, volume 18698.72396.

**The ratio at which it closes.** It is not a coordinate-to-radius ratio at
all — the geometry is exactly similar throughout. It is an absolute threshold:
the clamp starts costing refusals once `size < 1 mm` **and** the shape's
smallest genuine miss falls under `1e-4 mm`. For the D21 family
(smallest genuine miss ≈ `1.3e-4 · size` at `$fn` 96, `1.15e-4 · size` at 192)
the first refusal is lost at **`size ≈ 0.8 mm`** — an entirely ordinary fillet
radius — and total blindness arrives at `size ≈ 4e-6 mm`. A **genuinely small
feature on a genuinely large part** is exactly the case the clamp was said to
protect and is exactly the case it blinds.

## 4. Translation — the independent axis

Noise scales with **coordinate magnitude**; the floor does not. L-bracket and
T-bar (unions of two boxes, 40 mm) translated *inside* the `fillet()` so the
gate sees the far coordinates:

| model | r | offset x | blind | refused | max blind miss | floor | genus |
|-------|---|----------|-------|---------|----------------|-------|-------|
| L | 2, 1 | 0 … 1e6 | 0 | 0 | 0 | — | 0 |
| T | 2 | 0 … 1e6 | 0 | 0 | 0 | 2e-4 | 0 |
| T | 2 | **1e7** | 1 | **1** | **2.079e-01** | 2e-4 | 0 |
| T | 1 | **1e6** | 1 | **1** | **1.958e-03** | 1e-4 | **1** |

On geometrically exact creases a miss appears and is refused. It tracks
coordinate magnitude at about **2e-9 × |coord|**, consistent with the
repairer's 1.71e-7 at coordinates of order 40 (4.3e-9). Crossover:
`|coord| ≳ 5e4 × max(1, size)` mm — a part 50 m from the origin at `r = 1`,
reachable in a millimetre-unit site model. The cost is a **false refusal**, the
safe direction. Some numerical degradation exists at these offsets independent
of the gate (the L-bracket is genus 2 at `tx = 1e7` with **zero** refusals), so
these rows are reported as direction, not clean attribution.

---

## 5. Is `1e-4` the right constant?

Its *value* is defensible; its *shape* is not.

**On the value:** across 285 runs, lowering the floor 10× or 100× never
improved a single mesh — not on the one configuration that passes a genuine
sub-floor miss, not on any other defective configuration. And raising it is
demonstrably harmful only far above `1e-4` (at `k = 1e0` the canonical case
loses 59 refusals and is still genus 0 at `$fn` 48, so even that is slack).
There is no evidence for a different number.

**On the shape, two independent problems:**

1. **A constant fraction where the evidence is not constant.** The smallest
   genuine blind miss is a property of the *tessellation*, falling ~10× per
   doubling of `$fn`. Any fixed fraction of `size` is overtaken by refining
   `$fn`, and at 192 it already has been. This is a maintenance cost that comes
   due on its own schedule.
2. **`max(1.0, size)` is not size-relative at all.** It hard-codes a
   millimetre-scale assumption into a modeller with no units, blinds the gate
   below `r = 1`, and its worst case is the silent return of the exact D21
   shatter. It is the one thing here that is causally proven to break a solid.

**A better-founded discriminator.** The noise was *measured* to scale with
**coordinate magnitude**, not with a fixed millimetre and not with `size`. So
use the quantity the noise actually tracks:

```
floor = max(1e-4 * size, C * coordinateExtent)
```

with `coordinateExtent` the bounding-box extent of the mesh being gated and
`C ≈ 1e-7` — three orders above the measured `2e-9…4.3e-9` relative noise,
the same safety factor the repairer chose, and matching today's floor almost
exactly at coordinates of order 40 with `size ≥ 1`. It is scale-invariant in
both directions, it removes the millimetre constant, it closes the small-scale
blindness, and it makes the large-coordinate false refusals of §4 go away too
— one expression that fixes both axes instead of a constant that fixes
neither. This is a generalisation, not a patch: it replaces an arbitrary `1.0`
with the measured scaling law.

**Minimum acceptable change:** delete the clamp (`1e-4 * size`). Measured
byte-identical on every small-radius and small-scale control I ran — D21 at
`r = 0.05, 0.25, 0.5` at `$fn` 48 and 96, L-bracket and T-bar at `r = 0.5` and
at `s = 1e-3` — while turning the `s = 1e-5`/`1e-6` shatters into genus 0. At
`r = 0.5` it still clears the measured noise by ~290×.

## 6. Recommendation

**SHIP.** The change is a large net improvement (−15 / −42 / −22 → 0 / 0 / 1),
every false acceptance I could find is inert, and no configuration at ordinary
scale produces a shattered solid that a lower floor would save. The owner's
asymmetry holds: the errors this makes are refusals, and a valid solid with an
unfilleted crease beats a shattered one.

**Required follow-up, before this ships to users who model below 1 mm:**
replace `max(1.0, size)` with the coordinate-relative term above, or at minimum
delete it. It is the only defect here with a mesh to show for it, it is one
line, and it is byte-identical on everything else measured.

**Worth recording for the next person:** the `$fn` 192 headroom is 1.15×, not
1.9×, and it shrinks with tessellation. `1e-4` is not wrong today; it is on a
schedule.

## 7. `19468.9342` — untraceable, and the name is overloaded

`git log -S 19468.9342 --all` returns exactly one commit, `70dcb93c1`, and that
is the commit which *adds the refutation*. The document that first quotes it,
`handoff-2026-08-02.md`, is untracked and has no history at all. Every other
occurrence is a bare citation; not one is paired with a model that produces the
number.

Worse, **"boss-on-plate" names at least three different models** across the
record: `work/ab/ctrl_boss.scad` (the handoff listing made runnable) measures
**19479.3973**; the D21 review's reconstruction measures **19060.4526**; and
`work/ab/ctrl1.scad`, tabled as "boss-on-plate" in the BLOCKERS record, is an
all-box 40×40×4 with no cylinder at all and reads **9512.5968**. A near-miss
`19468.9341` — last digit 1 — appears once, in
`agent-ab23ecf2e337303cb/work/RESULTS3.md`, and is the only place any ~19468
figure sits beside a model.

Two related corrections: the "8 of 13 creases dropped for size" claim describes
a **different** model — a 40×40×**4** plate under a `round_tool` difference —
which is exactly why boss-on-plate's 6 mm plate emits no size warning at
`r = 2`. And of the three suspect literals, **`906.7936` is genuine and
reproduces**: `cube(10)` at `r = 2`, where `1000 − 906.7936 = 93.2064` is
precisely the edge-and-corner material removed.

So both earlier failures to reproduce `19468.9342` were correct. The literal
should be treated as dead and struck from the documents that cite it. The
control that survives all of this is the one used throughout this report:
**byte-identity of the exported mesh against a clean build**, which anyone can
rebuild, rather than a volume literal traceable to no model.

---

### Reproducing

```sh
BIN=<reviewed worktree>/build/OpenSCAD.app/Contents/MacOS/OpenSCAD
# the false acceptance (2 blind creases passed at 1.832e-04, floor 4.00e-04)
OPENSCAD_FILLET_SIZEGATE_DEBUG=1 $BIN -o a.off fa.scad | \
  awk '$0 ~ /FILLETGATE/ && $6=="tested=0"'
# the clamp collapse: D21 at scale 1e-6, $fn 48 -> genus -15, zero blind refusals
```

Instrumented worktree with the floor knob: `…/worktrees/margin-d21`
(reviewer-only; not for merge).
