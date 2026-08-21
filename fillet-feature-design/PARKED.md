# Parked decisions and open leads — 2026-08-21

Everything here is deliberately not in flight. Each entry names the decision or lead, the
evidence on record, and where the supporting material lives. Items leave this file by being
done or by being rejected with a reason.

## Decisions waiting on the owner

1. **cross fn=25 blade-refusal trade.** The blade-chain refusal (`476ee531d`) improved 55 cells
   and cost cross two cells one fold each (r=0.9: 2→3, r=1.5: 1→0→... later repaid to 0 by the
   both-runs trim `826b7af09`). Status: repaid — recorded here in case the sheets show anything
   the counts don't. Verdict needed: none, unless visual review disagrees.

2. **Refusal conservatism.** The 2026-08-20/21 gates (over-round, seat-escape, blade-chain,
   one-triangle) refuse far more at extreme radii: full-matrix refusal count 2110 → 4833,
   elbow_curved_endface stock 28 → 50 refused creases. Folds dropped 275 → 107 for it. "More
   sharp edges kept" is a product judgment: eyeball `fillet-bench/sheets/sheets-9-blade-refusal.pdf`
   and later.

3. **Chamfer-absorption semantics.** At `kSliverCreaseFrac = 0.25` (`5fbc11f9a`), a pre-existing
   chamfer narrower than ~r/5 is absorbed into the round instead of surviving as a flat strip
   inside it. Measured on a pre-chamfered-cube probe; only CW/R < ~0.15 moves. The agent's
   renders argue absorption is the CAD-correct answer. Confirm or revert the semantics.

4. **Sheet-5 retirement.** The atomic bench's construction pages (`fillet-bench/atomic/pages/K-*.png`)
   subsume sheet 5's job with asserted expectations. Keep both for now; retire sheet 5 when the
   atomic set has earned trust.

## Code-health simplifications (from the architecture audit, updated by measurement)

Audit: scratchpad `algo-map/fillet-algorithm-map.md` and the published artifact
"Fillet Algorithm Map". Tier-win table: commit message of `aa9c0955b`.

5. **Strip the Coons roll reconstruction** (`cornerRoll`/`rollProject` + relax/redraw inside
   `emitCoonsSaddle`): ~630 LOC and 14 of 45 constants. Doubly indicted since the audit: the
   tier table shows nothing depends on it structurally, and the atomic bench measured `coons`
   as an odd-$fn-only phenomenon (reached at $fn 19/32, never stock/64; AC16 is the clearest
   case). Risk: one corner to re-measure; fallback is `emitSaddle`.

6. **Merge the four ear clippers** (`earClipPlanar`, `earClip2`, `earClipRing`,
   `fillPlanarPolygon`): 227 LOC of one algorithm written four times → ~110. Zero behaviour
   change; bench must be byte-identical.

7. **Ladder-tier decisions, now falsifiable.** Measured wins over 451 kept builds: pullIn 410,
   fullMitre 24, baseline 14, unsplit 2, turnPull 1, noFuse 0. Nothing is safely deletable
   except possibly noFuse — and it is the section-fuse escape hatch, so deletion needs its own
   argument, not just the zero.

8. **Mitre-limit / fold-survivor / seat-escape constant cluster** (`kMitreLimit`,
   `kFoldSurvivorSetbacks`, `kSeatEscapeSizes`): three constants triangulating one geometric
   fact ("a ball of radius size at u cannot touch anything further than size off the face").
   Candidate: derive the first two from the third. Medium confidence; re-run the seat-escape
   verification if attempted.

## Geometry leads (specified, not started)

9. **Developed-domain footprint — parked as a patch.** ~470 LOC building `developSurface`/
   `emitDevelopedPatch` (BFS unfold, wrap-by-translation tiling, collar-only re-cut). Measured
   0/1400 kept builds: the target wall does not develop while the swallowed-fan dissolve makes
   secant triangles (angle deficit → refusal). Revive only after a dissolve that keeps walls
   developable, or an angle-deficit-tolerant chart. Patch + revival criteria: scratchpad
   `developed-footprint/developed-footprint-full.patch` + `REVIVAL.md` (against `cb88d96`).
   This is also what the remaining elbow_curved_endface fn48/64 wall folds (~12) wait on.

10. **Stronger centre-slide.** The spiral-section fix (`65560cb8f`) slides the section centre
    onto the feet's perpendicular bisector. The geometrically complete version (bisector plus
    `h = sqrt(size² − half²)` stand-off) is right on paper but regressed 2 cells when tried;
    needs its own judged tier.

11. **elbow_rot30 r=1.5 plate lamina.** After the terminus bite (`d0af9832a`), 2.1–3.0 mm² of
    coplanar lamina remains on its z=0/z=3 plate faces — large face triangles overlapping each
    other, a different, pre-existing fault. Own item.

12. **Adopt the lamina detector into the bench.** The fold census cannot see coplanar
    anti-normal laminae (that is how the terminus wedge hid). Detector: scratchpad
    `terminus/lamina.py`. Wire it into sweep.sh or the atomic runner as a guarded metric.

13. **Boolean/hybrid question — closed by argument, optional to close by measurement.** Opinion
    (with the hybrid §8): scratchpad `boolean-return/boolean-return-opinion.md`, published as
    the "boolean-return-opinion" artifact. The defensible core is the REQUIREMENTS option-3
    local clip, held until the seating cluster's maintenance cost is measured. Optional E2
    attachment probe (half a day) if wanted.

## Operational

14. **Expectation re-derives are routine.** The atomic bench refuses restamps on a stale
    binary; every geometry commit invalidates `atomic-measured` stamps. Re-derive via
    `atomic.sh --accept "<reason>"` and commit the `expect-changes.log` diff with the sheets
    regen.
