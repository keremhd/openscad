# D21 adversarial review — commit `61b92744f`

**Verdict: DO NOT SHIP.** The mechanism, the diagnosis and the D21 repair all hold
up and reproduce exactly. One blocker: the fallback reuses a fit/no-fit epsilon
that is five orders of magnitude below the noise floor of the samples it is
newly applied to, and as a result it silently drops fillets on ordinary
right-angle box models that HEAD rounds correctly.

Review build: my own worktree from `61b92744f`, built with `OpenSCADExe`; the
binary is **byte-identical output** to the reviewed worktree's binary on
`d21_48` (`cmp` on exported STL), so every measurement below is of the reviewed
code. Baseline is the clean `944e0cbef` build at
`/private/tmp/claude-501/-Users-kerem-Devel-openscad/a47a8762-.../scratchpad/wt-head`;
`OPENSCAD_FILLET_SIZEGATE=legacy` on the new binary is byte-identical to that
HEAD binary on every D21 tessellation, so the A/B below uses one binary in two
modes.

**Metrics derived before use.** Genus is taken from OpenSCAD's own
`--summary all` (index space). An STL-side Euler characteristic is *not* usable
here: `float32` export collapses distinct vertices, so `d21_96` reads 23
">2-face" edges and a fractional genus from the STL while the manifold itself is
genus 0 with 7935 vertices (STL welds to 7914). The STL metric was checked on a
cube (genus 0), a plate with a hole (genus 1) and a 36 860-triangle sphere
(genus 0) before being trusted for volume only. All models set
`$fa = 360/$fn; $fs = 0.01;` — omitting `$fa` changes the answer materially
(sphere-on-plate at `$fn` 192 gives a different volume and a different genus
without it).

---

## BLOCKER — the fallback refuses creases on double round-off

`checkChainSizes` fires the new fallback when

```cpp
nTested == 0 && nExempt > 0 && offExempt > 1e-9 * std::max(1.0, size)
```

The threshold is copied from the tested path, where the comment justifies it as
"six orders above that noise; a size that really does not fit misses by a
fraction of itself, never by a nanometre". That reasoning does not transfer.
The tested path only ever sees samples that are *not* chain ends and *not* beside
a junction; those land in a face's interior and `offFace` is exactly 0 or
macroscopic. The exempt samples are precisely the ones that sit **on** a face
boundary — the configuration the same comment describes as "the nearest point
and the boundary … the same point computed two ways". Their noise floor is not
`1e-16`; measured, it is **`1e-10` to `2e-7` mm** on models with coordinates of
order 40. The threshold at `r = 2` is `2e-9`. The rule therefore decides
fit/no-fit on the last bits of a double.

**Reproduction (30 s), an L-bracket — two cubes:**

```openscad
$fn = 48; $fa = 360/48; $fs = 0.01;
fillet(r = 2) union() { cube([40,30,6]); cube([6,30,30]); }
```

* `OPENSCAD_FILLET_SIZEGATE=legacy`: **no warning at all**, all 15 creases
  filleted, genus 0, 4548 triangles, volume 11187.1823.
* default: `WARNING: fillet: radius 2 does not fit 2 of the 15 crease(s)
  selected; … the blend would leave the surface it is meant to meet, by
  **2.06165e-08**.` genus 0, 4332 triangles, volume 11194.6677.

The refused creases are the two 6 mm top edges of the vertical arm. In the new
output the mesh contains vertices at exactly `(0.003, 0, 30)` and
`(5.997, 0, 30)` — the sharp edge is present; in the legacy output no vertex
lies on that line, i.e. it is rounded. **A user asking for `fillet(r=2)` on an
L-bracket now silently gets two unrounded edges.**

`OPENSCAD_FILLET_SIZEGATE_DEBUG=1` on that model shows why it is arbitrary. Four
chains are blind (`tested=0`, 5 samples each); by symmetry they are the same
kind of short edge, and their discarded evidence is

```
chain 3   maxOffFace_exempt=2.06165e-08   verdict=1  (refused)
chain 7   maxOffFace_exempt=2.06165e-08   verdict=1  (refused)
chain 11  maxOffFace_exempt=4.44089e-16   verdict=0  (kept)
chain 14  maxOffFace_exempt=0             verdict=0  (kept)
```

Two identical situations, opposite answers, separated by eight orders of
magnitude of arithmetic noise.

**Breadth.** A census over 8 box-family models × `$fn` ∈ {24,48,96} × `r` ∈
{0.5,1,1.5,2,2.5,3} (144 runs, gate instrumented) finds **28 combinations that
refuse a crease on evidence below 1e-7 mm**, across three model families:

| model | combinations firing on noise | worst noise |
|---|---|---|
| L-bracket `union(){cube([40,30,6]); cube([6,30,30]);}` | 9 of 18 | 1.71e-07 |
| T-bar `union(){cube([40,30,6]); translate([17,0,6]) cube([6,30,24]);}` | 8 of 18 | 2.45e-08 |
| plate + two ribs `cube([50,50,5])` + two `[4,50,15]` ribs | 10 of 18 | 2.01e-08 |

End-to-end confirmation on the T-bar at `$fn` 48, `r` 2: legacy emits **no
warning** and rounds all 18 creases (5944 tris, volume 11190.1726); default
refuses **3 of 18** at a worst miss of `1.86579e-08` (5620 tris, volume
11201.4007). Same story on the plate-with-ribs (10 → 11 refused).

This is the regression the owner's standard names explicitly: *a silently
unfilleted crease on a model that used to work*. It is also discontinuous in
`$fn` — the L-bracket at `r = 2` is clean at `$fn` 24 (noise 8.4e-10, under the
threshold) and broken at 48, 96 and 192 — which is the "bad but uniform beats
excellent but discontinuous" test failed in the wrong direction.

**Why this is a threshold bug and not a reason to abandon the fix.** On D21
itself the 87 chains the rule refuses carry discarded evidence of **0.0229 mm to
16.2274 mm** (median 0.986). Raising the fallback's floor to a size-relative
quantity — `1e-4 * size` would do, four orders above the worst noise measured
and two below the smallest genuine D21 miss — keeps all 87 D21 refusals and
kills every false positive found here. The fallback's *idea* survives; the
epsilon it inherited does not.

---

## Claims verified

**The mechanism and the diagnosis (§1–§4 of `work/D21.md`) — all reproduce.**
On `d21`, `$fn` 48, the round pass over the blended solid reads **104 chains,
1621 samples, 397 tested, 101 chains with zero tested samples** — exactly the
quoted tally. The builder's own census on the blended solid reads **380 convex /
362 concave** feature edges, matching §2 to the unit, which is the re-derivation
after the sorted-edge-key bug; the convexity figures were re-derived. The §3
arithmetic checks out: at φ = 153.22°, `r` = 4, `r/cos(φ/2) = 17.27` and
`r·tan(φ/2) = 16.81`. The warning's 16.2274 is the same number the debug output
reports as the worst discarded sample.

**The headline table — exact.** Measured with `--summary all`:

| `$fn` | HEAD genus | new genus | HEAD volume | new volume |
|---|---|---|---|---|
| 24 | 3 | 0 | 18446.3239 | 18723.1153 |
| 48 | −15 | 0 | 18533.0046 | 18898.0699 |
| 96 | −42 | 0 | 18436.0671 | 18942.5790 |
| 192 | −22 | 1 | 17771.1913 | 19155.8535 |

Every figure matches. HEAD's volume diverges downward, the new one converges
upward and monotonically toward the raw solid's 19230. STL ">2-face" edges read
6→0 at `$fn` 48 and 149→0 at 192 on my weld (claimed 5→0 and 152→0) — the small
difference is weld tolerance on `float32` export, the direction and the zeros are
exact.

**Rounded cube at `$fn` 192 is nondeterminism, not the rule.** Verified both
ways. Three runs of the *clean HEAD binary* on the same file give three
different md5s with 24156 triangles each and volumes 907.6531387293601 /
…603 / …605 — identical to 13 significant figures, differing in the last two
ULPs. Same phenomenon class as the known hole-in-a-flat-plate case (identical
volume, differing vertex order); this one differs in the last ULPs of the
volume as well, so it is vertex *order* plus summation order, not a geometric
change. And the "rule provably never fires" claim is confirmed by
instrumentation, not inference: `OPENSCAD_FILLET_SIZEGATE_DEBUG=1` on the
rounded cube at `$fn` 192 shows 12 chains, **every one with `tested=1`** — there
is no blind chain, so the fallback's `nTested == 0` guard cannot be reached.

**Sphere-on-plate at `$fn` 192 is a real repair.** Reconstructed the control
(`cube([60,40,6])` + `translate([20,20,6]) sphere(r=10)`, `fillet(r=2)`) — it
reproduces their `$fn` 48 volume 16618.606094 to the digit. At `$fn` 192 with
`$fa` set: legacy **genus −4**, volume 16633.965897, 3 of 20 creases refused;
default **genus 0**, volume 16640.146879, 8 of 20 refused. Both volumes match
their table exactly. The repair is caused by the rule (five more creases
refused), it turns an invalid solid into a valid one, and the 0.037 % of volume
moves *upward* — the round tool removes less — which is the correct direction
for a tool that was gouging. This control was broken on HEAD and is fixed.

**The window edges — no new discontinuity, and the fix is broader than
claimed.** Tilt swept at 30/40/42/44/45/46/48/50/55/60/65/70/75/80/84/86/88/89/90
at `$fn` 48 and 96, `Rb = 3`, `r = 4`. Below 50° nothing is refused in either
mode and the outputs are byte-identical, so there is no "fine at 46, refused at
45" cliff — the 45° cliff is the pre-existing concave size gate, unchanged. The
shatter band is *wider* than the reported 60–75: legacy is genus −4/−5/−15/−17/1
at 50/55/60/65/70 (`$fn` 48) and −12/−42/−42/−10/−2 (`$fn` 96), and the rule
takes **every one of them to genus 0**. At the upper edge, 84° (`$fn` 96, genus
3 → 0) and 89° (`$fn` 48, genus −1 → 0; `$fn` 96, volume 17818.70 → 18924.37 at
genus 0 either way) are further repairs. No tilt in the sweep is made worse.
The equal-radius case (`Ra = Rb = 10`, `r = 2`) is byte-identical at `$fn` 48/96/192.

**Canonical controls are untouched.** Byte-identical between the two modes at
`$fn` 48/96/192: boss-on-plate (19060.4526 / 19073.7172 / 19077.0393),
crossing pipes perpendicular `Rb=6` (20427.6172 / 20471.0676 / 20481.9626),
equal-radius tee, the thin-slab `round_tool` idiom, the inner-corner
`fillet_tool` idiom, and the rounded cube at `$fn` 48. A 28-model broad census
at `$fn` 24/48/96 (bosses, pockets, holes, 4/5/6-sided prisms, ribs on cylinder
and sphere, cross-drilled bar, cone, two bosses, near-wall, slot, chamfer ring,
Steinmetz, tee, thin plate, sphere alone, wedge, and both brush idioms) shows
**only** the L-bracket changing for a reason other than baseline nondeterminism.

**The tree is clean.** The commit touches exactly three files; `git status` is
clean; the sagitta experiment is gone — the only `getenv` calls left in
`src/geometry/fillet/` are the two documented ones. No stray diagnostics are on
by default.

**The unit suite.** HEAD 1688 assertions / 83 cases, all pass. New binary,
default mode: **1696 assertions / 84 cases, all pass** — exactly as claimed.

---

## The pin is right, its stated reason is not (report separately)

The needle test's *assertions* are sound and its derivation is real arithmetic,
not numerology. Dumping the verdicts directly for
`Manifold::Cylinder(120, 3, 0, 3)` at threshold 20°: 6 two-vertex chains — three
base edges and three slant edges — and at `r = 70` all six are `OffFace` with
amounts **66.4313 ×3 and 121.0920 ×3**. The claimed "66–121 mm" is exact, all
six exceed the `> 35` bar with margin, and under `legacy` the fault is not
`OffFace` at all (the amounts are 25.2754, 5.1962, 0.0, 19.2363 and two above
35) — so the test does discriminate the *reason*, which is what makes it worth
having. The `19.2` in the commit message is one of those six.

But the comment in the test, and the same sentence in `work/D21.md`, says the
`r = 70` verdict is "the same verdict the same creases already get at radius 1,
only larger". It is not. At `r = 1` the three base edges return **`Fits`**
(amount 0) and only the three slant edges are refused, at 0.1557 / 0.1557 /
1.5725. Half the creases change verdict between the two radii. The pin is
correct; the sentence justifying it is false and should be rewritten.

## Smaller notes

* `OPENSCAD_FILLET_SIZEGATE=legacy` leaves the unit suite **red**: the new test
  fails 5 assertions in that mode (`0 == 6` on the `OffFace` count, and four
  amount checks). `work/D21.md` is strictly accurate — it claims both modes
  green only for the pre-existing 1688 — but anyone bisecting with the env var
  set will see a failing suite. Worth a `SKIP` guard or a note.
* The 50–70° band is repaired by refusing essentially the whole convex pass
  (274/276, 266/268, 252/255, 175/178 creases refused). That is inside the
  owner's "a valid solid with an unfilleted crease beats a shattered one", and
  legacy already refused 65–78 % there, but the outcome should be described as
  "the round pass is switched off on this shape", not as "the runout lip is
  dropped".
* The debug channel writes to `stdout` via `fputs`. Harmless while off by
  default, but it would corrupt `-o -` export if ever enabled there.
* The canonical boss-on-plate volume quoted in the handoff (19468.9342) does not
  reproduce from the handoff's own listing — I measure 19060.4526 at `$fn` 48
  and 19077.0393 at 192, identical in both modes. Pre-existing; unaffected by
  this change; flagged only so it is not mistaken for a regression later.

## Not checked, and why

* **CGAL cross-check on D21** — under `--backend=cgal` the oblique case produces
  no inner bead in any mode, so it cannot corroborate anything here.
* **The `fillet-tests/` case suite** against `expectations.txt` — the harness
  routes through CGAL dilation and did not fit in the time left; the 28-model
  census above was run instead and covers the same shape families.
* **The 153.22° / 175.52° dihedral extremes and their coordinates** — I verified
  the 380/362 counts they were re-derived alongside, and the setback arithmetic
  that follows from 153.22°, but not the dihedral measurement itself; that would
  need its own instrumented census, and the traps here say a crease classifier
  is exactly the kind of metric that saturates silently.
* **Whether genus 1 at `$fn` 192 on D21 is benign** — it reproduces, it is one
  handle rather than a shatter, and the volume is the best of the four, but I
  did not localise the handle.
