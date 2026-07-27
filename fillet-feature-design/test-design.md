# Fillet test design

How the fillet operator gets tested, which layer each kind of check belongs in,
and what has to change in the build to make that possible.

Companion to [`fillet-operator-plan.md`](fillet-operator-plan.md) (§15 test set)
and [`detailed-milestones.md`](detailed-milestones.md) (M1, M6).

---

## 1. What this repo already does

Two independent mechanisms, both driven by CTest.

### 1.1 Regression tests — the dominant mode

Python-harness tests (`tests/test_cmdline_tool.py`, `tests/image_compare.py`)
that run the `openscad` binary over a `.scad` file and diff the output against a
committed baseline in `tests/regression/<testapp>/`.

The relevant glob for us is `tests/CMakeLists.txt:269`:

```cmake
file(GLOB FEATURES_3D_FILES ${TEST_SCAD_DIR}/3D/features/*.scad)
```

which feeds, transitively, four test apps — and each of the image ones has both
a CGAL and a Manifold variant:

| test app | output | line |
|---|---|---|
| `dump` | `.csg` text | `tests/CMakeLists.txt:842` |
| `render-cgal` / `render-manifold` | `.png` | `:849`, `:854` |
| `preview-cgal` / `preview-manifold` | `.png` | `:861`, `:865` |
| `throwntogether-cgal` / `throwntogether-manifold` | `.png` | `:870`, `:871` |

So **one `.scad` dropped in `tests/data/scad/3D/features/` becomes seven test
instances**, with no CMake edit. Baselines are produced with
`TEST_GENERATE=1 ctest -R <name>`, then eyeballed and committed. This is the
procedure in `doc/testing.md` §"Adding a New Test".

Scale today: ~84 files in `3D/features/`, ~120 directories under
`tests/regression/`.

### 1.2 C++ unit tests — small but real

Catch2 v3. Target `OpenSCADUnitTests` (`CMakeLists.txt:1769-1805`), registered
into CTest via `catch_discover_tests(... ADD_TAGS_AS_LABELS)`, so unit tests run
as part of a normal `ctest` invocation. The binary sits next to `openscad` in the
build dir and takes the usual Catch2 filters.

Three `*_test.cc` files exist:

| file | lines | what it does |
|---|---|---|
| `src/utils/vector_math_test.cc` | 249 | pure math: point-line, seg-seg, line-line distance |
| `src/gui/Measurement_test.cc` | 86 | GUI-side measurement |
| `src/geometry/linear_extrude_test.cc` | 170 | declares `LinearExtrudeInternals::` helpers `extern`, tests them directly, plus a Manifold-vs-CGAL equivalence case |

`linear_extrude_test.cc` is the closest precedent for what fillet needs: it
reaches into a geometry module's internals by re-declaring them at the top of the
test file rather than exporting them from the header.

### 1.3 Known defect in the build — geometry unit tests are not compiled

`CMakeLists.txt:1578-1583`:

```cmake
file(GLOB_RECURSE TEST_SOURCES "src/utils/*_test.cc")
file(GLOB_RECURSE GUI_TEST_SOURCES "src/gui/*_test.cc")
```

`GLOB_RECURSE` only recurses beneath the directory part of the pattern, so
`src/geometry/` is not covered. `linear_extrude_test.cc` is also stripped from
the main library by `list(FILTER Sources EXCLUDE REGEX ".*_test[.]cc")` at
`:1584`. It is therefore compiled into nothing.

Confirmed against the local build tree — the only test objects present are:

```
build/CMakeFiles/OpenSCADUnitTests.dir/src/utils/vector_math_test.cc.o
build/CMakeFiles/OpenSCADUnitTests.dir/src/gui/Measurement_test.cc.o
```

The file has been dead since PR #6407 introduced it. **Any plan to unit-test
fillet internals must fix this glob first**, and should expect
`linear_extrude_test.cc` to need repair once it starts building again. That is a
pre-existing upstream bug, worth a separate commit from the fillet work.

---

## 2. The split we adopt

Regression `.scad` tests are the repo norm and stay the **primary** vehicle —
§15's test table maps almost one-to-one onto them, and the M1/M6 plan is already
the idiomatic shape. But this feature carries an unusual amount of pure,
deterministic, non-visual logic, and a PNG diff is a poor instrument for it: a
sign error in the concavity test or a NaN escaping the trihedral solve shows up
as an ambiguous pixel cluster in a throwntogether image, hundreds of lines away
from the cause.

Rule of thumb:

> **If the correct answer is a number, a boolean, or a count, it is a unit test.
> If the correct answer is a shape, it is a `.scad` regression test.**

### 2.1 Unit-test layer (Catch2)

| what | milestone | why not a PNG |
|---|---|---|
| Edge → two-face adjacency rebuild from `triVerts`: merge-by-position, undirected pair matching, every-edge-has-exactly-two-faces invariant | M2 | pure combinatorics; assert on counts and the invariant directly |
| Far-vertex concavity test (§4.2) | M2 | the answer is one boolean per edge; a sign flip is invisible until it becomes a gouge |
| `$fa`-derived tessellation-seam filter; classification stability at `$fn` = 8/16/64 | M2 | assert *identical* classification across three tessellations of one solid — cheap here, near-impossible to see in an image |
| Chain walking; per-vertex normals; `C`, `TA`, `TB` (§5) | M4 | chain topology (open/closed, ordering, length) is a data structure |
| Trihedral solve, dual truncation, corner cell (§6.3) | M8 | check the solved point against the closed-form answer for the symmetric pyramid case (§6.3.1) |
| Valence-4 Q-vertex: multiple feasible triplets, infeasible triplet discarded | M9 | "the infeasible solution was discarded" is a set-membership assertion, not a picture |
| Singular solve rejected, not NaN (§6.3.2) | M9 | `REQUIRE(std::isfinite(...))` — a NaN may render as *nothing*, which looks like a pass |
| `r` larger than the face → clamp or warn, never garbage | M9 | assert on the clamped value and the warning |
| ID range membership / `ReserveIDs` stamp (§8) | M11 | O(1) range test, flagged as the riskiest step in the plan; test it in isolation before trusting it |

### 2.2 Regression layer (`.scad` + committed baselines)

Everything from §15 that is fundamentally about shape:

- cube − cylinder through hole (the §1 sanity case) and blind hole
- cube outer edge, `round_tool`, convex sign flip
- inside corner (floor + wall), the §5.3 worked example
- inside box corner, three faces
- symmetric square pyramid pocket; asymmetric valence-4 junction
- TLC123 three-cube cross (the #884 failure case — our correctness bar)
- fillet-then-fillet with larger `r`; chamfer-then-fillet with and without `ignore_tags`
- brush covering half a chain; brush missing entirely; brush boundary on a spine vertex
- imported STL, angle-threshold fallback path
- M0-level: the four builtins parse and appear correctly in the `.csg` dump

Note the overlap is deliberate, not redundant. The valence-4 and pyramid cases
appear in **both** layers: the unit test pins the solved vertex coordinates, the
regression test proves the solid built from them is the right solid.

---

## 3. Backend constraint

`GeometryEvaluator::visit(State&, const FilletNode&)` requires Manifold and warns
`"fillet tools require the Manifold backend"` otherwise
(`src/geometry/GeometryEvaluator.cc:999-1015`).

The `3D/features/` glob does **not** know that. A fillet `.scad` dropped there is
automatically also run under `render-cgal`, `preview-cgal` and
`throwntogether-cgal`, where it will render an empty or unfilleted result.

Two options, decided at M6:

1. Commit the degraded CGAL baseline as the expected output. Honest, zero CMake
   churn, and it pins the warn-and-passthrough behaviour — but it bakes in
   images that look wrong to a reader.
2. Exclude the fillet files from the CGAL variants, via the existing
   `RENDER_DIFFERENT_EXPECTATIONS` / `disable_tests_safe()` machinery
   (`tests/CMakeLists.txt:25-38`).

Preference is (2) for the geometry cases and (1) for a single dedicated
`fillet-backend-fallback.scad` that exists specifically to pin the warning.

Either way this is a required M6 decision, not an implementation detail — the
first fillet file added to `3D/features/` triggers it.

---

## 4. Current scaffolding and where it goes

`fillet-tests/` at repo root, outside CTest entirely. See its `README.md` for the
case contract; the design decisions behind the shape are here.

### 4.1 Why one directory

M1 built two: `fillet-visual-tests/` (hand-written references + `render.sh`) and
`fillet-equivalence-tests/` (`check_equivalence.sh` + `eq_*.scad`). They were
correct as scaffolding but structurally guaranteed to diverge — a case had to be
written twice, once as a scene and once as a comparison, so the automated set
lagged the visual set permanently (7 scenes against 3 comparisons, of which 2
were harness self-tests). Merged 2026-07-27 into `fillet-tests/`, where a case is
one file declaring model, reference tool and operator call, and both the renderer
and the checker drive it. **Adding a case is one edit and yields both.**

Metadata (sign, slice, size variants, tolerance) lives in the `.scad` and is read
back by the shell through OpenSCAD's echo export, so there is no manifest to fall
out of sync with the geometry.

### 4.2 Side by side *and* diff

The renders show six columns: model, reference applied, candidate applied,
reference tool, candidate tool, and the residual between the last two. The
residual column is the same geometry the `tool` check evaluates, so an empty red
column and a green test are one fact rather than two opinions. Side by side
diagnoses; the diff decides.

### 4.3 Three checks, so the junction cases are not eyeball-only

| check | needs a reference | catches |
|---|---|---|
| `tool` | yes | wrong radius, missing corner, gouge, wrong sign |
| `sandwich` | **no** | gouges, runaway or wrong-direction tools |
| `emits` | no | the operator silently doing nothing |
| `drops` | no | a size the feature cannot carry being built anyway, or refused in silence |

`sandwich` — result and model must lie within `size` of each other — is what
gives the trihedral and valence-4 corners automated coverage despite having no
closed form to compare against (§2.2 lists them, M8/M9 build them). It is
deliberately coarse: `selftest_wrongradius` passes it, which is the calibration
worth knowing.

Radii too large for the face used to be lumped in with the junction corners as
"no reference", which conflated two different things. §6.5 now settles them: an
oversized radius is warned about and dropped, which is a perfectly writable
answer, so those variants declare `kind = "drop"` and get `drops` — empty tool
**and** the warning — rather than being excused from comparison. Only the
junction corners are genuinely reference-free (`kind = "none"`). `drops` is also
the suite's first check that reads the log rather than the geometry, because
emptiness is precisely what a refusal and an unimplemented operator share.

### 4.4 Cost

Both tolerance checks dilate through Minkowski, which has no Manifold path and
falls back to CGAL's Nef kernel. Two consequences, both handled in the drivers
rather than by dropping cases:

- Ring cases run at `$fn = 48`, not 128. Chord error is ~0.2 % of `r`, far under
  the 2 % tolerance, and Minkowski cost climbs steeply with facet count — at 128
  the boss case did not finish in six minutes.
- Every check and render is bounded by `CHECK_TIMEOUT` (default 60 s), and a
  signal death is reported as `CRASH` rather than folded into `FAIL`, so an
  engine bug can never masquerade as a geometric verdict.

### 4.5 What still moves later

M6 converts the cases that are green by then into `.scad` files under
`tests/data/scad/3D/features/` with committed baselines. The two-sided
containment comparison itself is better expressed as a Catch2 test calling
Manifold directly — same shape as the Manifold-vs-CGAL equivalence case in
`linear_extrude_test.cc` — rather than ported to CTest as a shell script. That
would also sidestep the CGAL Minkowski cost above. `fillet-tests/` stays
afterwards as the place a case is prototyped before it has a baseline.

---

## 5. Ordered actions

1. **M2, prerequisite** — add `src/geometry/*_test.cc` to `TEST_SOURCES`
   (`CMakeLists.txt:1578`). Separate commit; fix or delete
   `linear_extrude_test.cc` in the same change, since it will start building.
2. **M2** — `src/geometry/fillet/FilletBuilder_test.cc`, following the
   `linear_extrude_test.cc` pattern. This requires the adjacency and
   classification types to be reachable from the test.

   **Decided (2026-07-27): dedicated internal header.** The internals
   (`Tri`, `EdgeKey`, adjacency rebuild, edge classification) move out of the
   anonymous namespace in `FilletBuilder.cc` into a new
   `src/geometry/fillet/FilletBuilder_internal.h` under a named namespace
   (`fillet::detail`), `#include`d by both the `.cc` and the test. The public
   `FilletBuilder.h` stays a single entry point (`buildFilletTool()`). Chosen
   over the `linear_extrude_test.cc` re-declare-`extern` trick because the
   internals include *structs*, not just free functions — re-declaring a struct
   layout by hand in the test would drift; a shared header keeps one source of
   truth. Chosen over widening the public header because these types churn
   through M4–M11 and should not become public surface.
3. **M6** — establish the baseline-PNG pattern once, with the §1 sanity case and
   the chamfer cases; resolve the CGAL-backend question from §3 here.
4. **M4, M8, M9, M11** — each geometry milestone adds unit tests for its own
   invariants per §2.1, and a `.scad` per §2.2. After M6 the regression side is
   just "add a file".

## 6. Non-goals

- No new test framework or harness. Catch2 and the existing Python cmdline
  harness cover everything above.
- No unit tests for tool-solid *shape*. Comparing meshes for geometric equality
  is exactly what the regression layer already does well.
- GUI tests (`src/guitests/`) are out of scope; fillet adds no GUI surface.
