# Traps, broken instruments, and corrections to the record

Extracted from the handoffs so it survives their archiving. Each item cost hours at least once.

## The standing rule

**Run any new metric on a case whose answer is already known**, and build a self-proving
invariant into anything new — a shape rotated by exactly one facet is the same solid and must
measure the same. Nine instruments have been found broken; that is the defining methodological
hazard of this effort.

**Check the feature actually fired before believing a win.** Report served-vertex, resample and
refusal counts beside every number. One reported win was byte-identical inertness.

## Traps in this environment

1. **Builds are silently discarded, and `make` reports "Built target" for binaries that do not
   exist.** Verify the binary's mtime is newer than your last edit before trusting any
   measurement. The GUI target is `OpenSCADExe`, not `OpenSCAD`. If builds misbehave, retry
   with `dangerouslyDisableSandbox`.
2. **`env $VAR ...` in zsh performs no word splitting**, so a second probe is silently dropped.
3. **This machine's locale uses comma decimals** — `atof`/`strtod` on `"0.1"` yields 0. Use
   `istringstream`/`ostringstream` imbued with `std::locale::classic()`.
4. **The crease threshold is `1.5 × max($fa, 360/$fn)` and `$fa` defaults to 12**, so a model
   setting only `$fn` is measured through an 18° threshold. Set `$fa = 360/$fn; $fs = 0.01;` in
   measurement models and confirm the threshold moved. (This is D22 itself; until it is fixed,
   it is also a measurement trap. It voided an entire round of D20.)
5. **A radius that does not fit is warned about, skipped, and still exports a plausible mesh.**
   An export succeeding proves nothing — read the warnings.
6. **Derive genus from the mesh, not from ECHO output.** `>2`-face edge counts are
   weld-tolerance sensitive: 205 at weld 1e-5 against 0 at 1e-6 on the same mesh. State the
   tolerance; read for direction and zero only.
7. **`$fn` sweeps need a tessellated model.** `pocket.scad` and `rhomb.scad` are entirely
   planar, so sweeping `$fn` over them tests nothing. One report's whole series was vacuous.
8. **Brushes are children 1+, not a `brush =` argument** — that is a parse error.
9. **A 10-minute stall watchdog kills long silent commands.** Run bounded batches that stream
   output, checkpoint every row to a file, and **commit as you go**. Roughly eighteen agent runs
   have been lost to the watchdog, host process exit and power loss. Only committed work
   survived, every time.
10. **On a rotated bore, Manifold and CGAL do not evaluate the same target.** Phase tables must
    be one backend.
11. **Resource limits**: `cone_fn192` peaks at 156 GB and OOMs under CGAL in every binary,
    including clean base; the `fn384` models cannot be exported on this machine at all.
12. **Tooling**: plain `python3` only — no matplotlib, PIL, cairosvg, rsvg-convert. ImageMagick
    exists but its SVG rasteriser has no font.
13. `CCACHE_BASEDIR=/Users/kerem/Devel/openscad` before configuring any worktree, or each
    worktree builds from a different absolute path and ccache hits ~12 %.
14. **`ctest -R fillet` passing is necessary, never sufficient.** The shell-level `fillet-tests`
    suite is CSG scene-graph dumps plus fuzzy image compares on models the seam rule does not
    touch. The four models that would actually prove the geometry are slated for deletion before
    merge — do not delete them without a replacement.

## Instruments found broken

| # | instrument | what was wrong |
|---|---|---|
| 1 | crease classifier (D18) | saturated at the tessellation floor — readings sat exactly at facet angles 7.5 / 3.75 / 1.875 |
| 2 | fold census (D20 r1) | run through the `$fa`=12 artefact, so flat in `$fn` by construction |
| 3 | convexity census (D21) | signed off a sorted edge key — read 5-convex / 7-concave on a cube |
| 4 | fold census collar (D20 r2) | a cylinder about the *y* axis while every bore was 1.3 mm off it against a 0.10 mm collar, so it excluded nothing |
| 5 | census family name (D20 r4) | `sph` instead of `sph_across` returned an empty surface list; a byte-identical mesh read 150 folds at 173.8° instead of 114 at 65.3° |
| 6 | "1/37, same as HEAD" (D17.3) | byte-identical inertness — the feature had done nothing at all |
| 7 | `-o /dev/null` | makes OpenSCAD skip the render and report zero calls |
| 8 | `timeout(1)` | does not exist on this machine; made an export loop report 19/19 FAILED |
| 9 | Catch2 name splitting | splits on commas, so an unescaped exclusion excludes nothing and silently reports the full total |
| 10 | `bnd` as the D24 / A3 instrument | identically zero on every Manifold-backend export, by a parity argument, so it reads clean on a correct solid and on a broken one alike. `fillet-bench/README.md` gives the argument |

## Corrections to the earlier record

Each of these was believed and each was false. Recorded so they are not re-derived.

- `handoff-2026-08-03.md:274` — "all 13 controls byte-identical either way" is **false**; four
  move (`pocket`, `rhomb`, `slab`, `corner`). Likely carried from a D17.3-in-isolation control.
- The list of **15** nondeterministic corpus models omits `box_L_fn96_r1` and `box_T_fn96_r1`.
  The recorded "5 of 210" is one high; the raw "17 of 225" is correct and not understated —
  13 of the 17 are vertex-order noise.
- The "13 controls" are **12 distinct models**: `boss.scad` and `ctrl1.scad` are byte-identical.
- There are **two** `ScopedSizeGateRule` users, not three, and one of them is the
  scale-invariance test.
- `kSeamOverMax`'s comment claimed a ceiling of about twice 0.10·r. The sweep supports a
  **floor** at 0.08 and no upper edge short of 2.0 — and 2.0 was the top of the swept range,
  not a measured failure boundary. Corrected in source.
- The old `assert(!closed)` was dead twice over: compiled out under `-DNDEBUG` *and*
  unreachable, because eleven call-site guards already refused. What kept rings out was the
  guards, never the assertion the header credited.
- **Byte-identity is retired as an acceptance instrument.** 17 of 225 corpus models are
  nondeterministic in vertex order, and the integration record concluded that holding a branch
  carrying two default-on changes to byte-identity against clean HEAD is the wrong test.
