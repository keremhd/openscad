# Fillet operator — M6-prep: automated equivalence tests

Tessellation-independent, **automated** checks that the fillet operator's output
matches the hand-written reference geometry in `../fillet-visual-tests/`. This is
the "drop the operator in and test it" mechanism from `../detailed-milestones.md`
(milestone **M6**), staged early so the tests exist *before* the operator does.

## The idea: equivalence-by-emptiness

An exact symmetric difference of operator-vs-reference is never empty — the
operator tessellates arcs as circular-segment hulls at `$fa`, the reference uses
`cylinder`/`rotate_extrude` at `$fn`, so facets never line up (measured: ~256
stray facets). Instead we test **two-sided containment within a tolerance `t`**:

```
(candidate − dilate(reference, t))  empty   AND
(reference − dilate(candidate, t))  empty
```

⟺ Hausdorff(candidate, reference) < `t`. Pick `t` above the arc chord error
(~`0.004·r` at `$fa=12`) and below the smallest real defect worth catching
(default `t = 0.02·r`). Tessellation slivers vanish; a wrong radius / missing
corner / gouge survives.

`_fillet_test.scad` provides it as one module (candidate = `children(0)`,
reference = `children(1)`):

```openscad
use <_fillet_test.scad>;
fillet_equivalence(t) { candidate(); reference(); }
```

When the union is **empty**, OpenSCAD prints `Current top level object is empty.`
and exits 1. `check_equivalence.sh` turns that into a **passing** test.

## Running

```
./check_equivalence.sh eq_inner_corner.scad   # one case
./run_all.sh                                   # all, checked against expectations
OPENSCAD=/path/to/openscad ./run_all.sh        # override the binary
```

`check_equivalence.sh` exit codes: `0` = PASS (empty ⇒ equivalent), `1` = FAIL
(non-empty leftover ⇒ shapes differ), `2` = ERROR (binary missing / parse error).

## Cases and expected state at M0

| file | role | expected now |
|---|---|---|
| `eq_selftest_equal.scad`       | same bead, `$fn` 96 vs 48 — proves tessellation is tolerated | **PASS** |
| `eq_selftest_wrongradius.scad` | r=8 vs r=6 — proves real errors are caught | **FAIL** |
| `eq_inner_corner.scad`         | real `fillet_tool` vs hand reference | **FAIL** (red until **M7**) |

The two self-tests prove the check is neither always-pass nor always-fail. The
real case is **red today** because `fillet_tool` is the M0 no-op (returns empty);
it turns **green automatically** when M7 makes the operator emit the bead.

Add a new real case by copying `eq_inner_corner.scad`: define `candidate()` as the
operator call and `reference()` as the matching `_fillet_ref` idiom.

## Wiring into CTest (later, at M6)

Each case becomes one `add_test` whose command is `check_equivalence.sh <case>`;
the shell exit code is the pass/fail. This complements — does not replace — the
image-regression tripwire (drop the applied-result `.scad` into
`tests/data/scad/3D/features/` with a committed baseline PNG) and a Tier-1
validity check (`union(model, tool)` must render `Status: NoError`).

## Dependency note

These files `use <../fillet-visual-tests/_fillet_ref.scad>`, so this directory
depends on `../fillet-visual-tests/` (the M1 harness). Kept as a separate
directory for a separate commit.
