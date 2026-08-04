# Decision — `arrivesStraight` ships, and the grouping question is closed

2026-08-04. Owner decision, taken on the measurement in
`../measurements/2026-08-04-arrives-straight.md`.

## What was decided

`arrivesStraight` (`src/geometry/fillet/FilletBuilder.cc`) **stays**, unconditionally live, which is
where the env-var removal left it.

The owner's starting position was removal — *"I don't know its purpose... if doubting, I am veering
towards removing"* — and the measurement was commissioned precisely because no record justified it on
the merged tree. The measurement came back against that position and the position moved.

## Why

Removing it is a **false acceptance**, the dangerous error under the owner's standard that a valid
solid with an unfilleted crease beats a shattered one:

| model | kept | removed |
|---|---|---|
| `box_step_fn24_r3` | genus 0, 0 nme | genus −2, 4 nme |
| `box_step_fn48_r3` | genus −1, 2 nme | genus −4, 8 nme |
| `box_step_fn96_r3` | genus −5, 8 nme | genus −15, 28 nme |
| `mixp` | 4/15 bad | 10/15 bad (TW=1), 12/15 (TW=4) |
| `bcurve` | 13/37 bad | 1/37 bad |
| `mixc` | 14/37 bad | 1/37 bad |

Three corpus models degrade and none improve; the damage **worsens with refinement**, which by the
owner's first standard makes it a real defect rather than faceting. The two large wins are on models
outside the 225 corpus.

## What the D17 review's recommendation actually said, and why it does not apply

The review recommended deleting the local grouping, `anyServed` and `arrivesStraight` **together**,
and its cited `bcurve` 0/37 column is the **"ungated + global"** arm — two changes at once.
`06b301f2d` shipped the opposite half: local grouping made permanent. Deleting `arrivesStraight`
today therefore produces **"ungated + local"**, a combination the review never measured and never
recommended.

## The question that was closed rather than answered

Whether the **local grouping should have been made permanent** was decided by default when the env
vars were stripped, not by measurement. Settling it would have needed a third arm,
`removed + global subtraction`, which required new code because the global branch was deleted with
the env var.

**The owner closed this: global subtraction is broken and not worth building.** A comparison against a
mode that cannot ship has no value. The question is closed on that ground — not deferred, and not an
outstanding gap.

## AMENDMENT, same day — the fallback this predicate selects is itself invalid

Renders were commissioned after this decision was taken, on the owner's suspicion that *"putting a
bead in was also broken"*. **The suspicion was correct, and the evidence is a proof rather than an
impression.**

At a curved arrival the seated-bead fallback does not build a bead. It leaves a thin upright fin —
two sail surfaces meeting at a cusp, standing proud of both plate and boss wall, a fan of slivers
converging on one apex. Measured on `bcurve.scad` (run unmodified, threshold 8.44°, zero
"does not fit" warnings, `Status: NoError`):

| | `kept` (fallback taken) | `removed` (rule ungated) |
|---|---|---|
| Euler χ | **5 — odd** | 2 |
| mesh genus | **−1.5 — fractional** | 0 |
| edges with >2 faces | **5** | 0 |
| boundary edges | 0 | 0 |
| components | 1 | 1 |

A closed orientable surface cannot have odd χ or fractional genus. The `kept` arm at this corner is
**not a valid solid**. Stable at welds 1e-7 and 1e-6. Images:
`../arrives-straight-images/bcurve-corner-{shaded,mesh,zoom}-{kept,removed}.png`.

**What this does and does not change.** It does not reverse the decision: removing `arrivesStraight`
still degrades three corpus models and improves none, and that damage still grows with refinement.
But it removes one of the two pillars the decision stood on. The honest statement of what is now
known is:

> `arrivesStraight` is a predicate choosing between two bad constructions. It is kept because
> removing it measured worse overall, **not** because the branch it selects is sound. At a curved
> arrival that branch produces an invalid solid.

This is a live defect against the owner's standard that the bar is a valid solid, and it is not
tracked by any of D22/D23/D24. Whoever picks up this area next should treat "the seated-bead
fallback at a curved arrival" as an open defect in its own right, not as settled ground.

## Standing caveat for whoever reads this next

`arrivesStraight`'s original justification has expired. D17.3 justified it on one model — *"it earns
its lines by keeping one model off nine bad tessellations... that is the whole case for keeping it,
and it is a case about `rib.scad` alone"* — and it is now **inert on `rib.scad`** (all 37
tessellations byte-identical) although the census proves it fires there. It is kept for what it
protects **now**, which is not what it was chosen for. That is a legitimate reason to keep it and a
poor reason to trust the original rationale.

## Not measured

The unit suite was never run on the removed arm; a change moving `mixp` by eight cells would likely
move pins. No timing of the now-unconditional pass. CGAL backend not exercised — Manifold only.
