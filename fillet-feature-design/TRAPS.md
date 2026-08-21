> **HISTORICAL — mostly archaeology.** From the *old* swept-tool effort. Durable and still useful:
> ~6 environment facts (locale, ASan, the build watchdog, ccache, BSD sed, python tooling) and 3
> measurement rules (state the weld tolerance; quote vertex counts beside verdicts; run a new
> instrument on a known answer). Do not implement from the rest. Current spec:
> [START-HERE.md](START-HERE.md) → [REQUIREMENTS.md](REQUIREMENTS.md).

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
14. **The builder is nondeterministic in validity, not only in vertex order.** `rib_into_boss`
    returns two or three distinct meshes at **every** `$fn` tested, over eight identical runs of
    one unchanging binary; the topology moves, not the vertex order, and all runs exit rc=0. At
    `$fn`=14 it flips validity, 6 valid to 2 invalid. It produces no output at all at `$fn`=12
    on some runs, and exits **SIGBUS (rc=138)** at `R`=1.0 about one run in six. **One render
    per cell reports a real fault as clean roughly once in thirty**; measure each cell at least
    three times and aggregate to the worst outcome, as `fillet-bench/sweep.sh --repeat` does.
    `refused_neighbour` is deterministic and is the matched control. Every single-render
    measurement on this branch — the 225-model corpus and the original contact sheet included —
    was taken without this knowledge.
15. **An unwelded mesh reading is vacuous.** Manifold's output is 2-manifold by index
    construction, so a reader that does not weld cannot see a self-touch: it reads clean on a
    correct solid and on a broken one alike. This is instrument #10's parity argument in a
    second costume. Every disputed failure on record is invalid across weld 1e-4…1e-12 and
    "valid" only at 1e-15. State the tolerance and never read at one that welds nothing.
16. **`export_off.cc` prints six significant figures; `export_stl.cc` round-trips exactly.**
    The loss is real — two vertices 5.7e-7 mm apart print as one OFF line. Measured across 353
    cells it changes **no verdict** (10 cells lose a vertex, 0 change answer), so it is a
    precaution, not an explanation: do not attribute a failing set to it without checking.
    Note also that OFF writes polygons and STL triangles, so `f` and `e` differ on any quad —
    compare verdicts, never face counts.
17. **BSD `sed` has no `\|` alternation.** A pattern anchored on `off:` silently stopped
    matching when the reader moved to STL, and every row read `UNREADABLE` rather than erroring.
18. **AddressSanitizer does not run on this machine.** Any `-fsanitize=address` binary — down to
    `int main(){return 0;}` — hangs before `main`, spinning in
    `libSystem_initializer → __malloc_init → wrap_malloc_default_zone` inside
    `libclang_rt.asan_osx_dynamic.dylib`. macOS 26.5, Apple clang 17.0.0; `MallocNanoZone=0`
    does not help, and there is no Homebrew LLVM and no valgrind here. Building it costs about
    12 minutes for nothing. Use instead `-fsanitize=undefined
    -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE`, which bounds-checks
    `vector::operator[]` and traps with rc=133. **Verify the hardening is live on a three-line
    test first** — it is a define, and a build that silently dropped it looks exactly like a
    clean run.
19. **A macOS `.ips` crash report carries `vmRegionInfo`, and it names the fault class.**
    "N bytes before the start of a MALLOC_SMALL region" is an out-of-bounds index, not a stack
    overflow or heap corruption, and N divided by the element size is the index. That one line
    pinned the `chainBulges` fault to `pos[-1]` before any code was read. `lldb` on a
    RelWithDebInfo OpenSCAD is not a substitute — it did not finish loading symbols in ten
    minutes.
20. **A measurement is only comparable within one binary.** Three builds existed on 2026-08-04
    between 19:43 and 00:04, and a sweep begun under the first would have silently mixed them.
    Record the binary's mtime on every row, and re-check it at the end of a run.
16. **`ctest -R fillet` passing is necessary, never sufficient.** The shell-level `fillet-tests`
    suite is CSG scene-graph dumps plus fuzzy image compares on models the seam rule does not
    touch. The four models that would actually prove the geometry are slated for deletion before
    merge — do not delete them without a replacement.

22. **A rule that changes what it measures has to be re-run against the population that
    justified it.** Attempt 2 replaced a linear foot-off-mesh distance with an angular residual
    and was measured only on the false refusals it was built to cure. Re-run against the
    artifacts, at the production threshold it catches **32 of 72 and 76 of 91** where the
    distance caught 72 and 91 — and the ones it misses read machine zero, *below* the genuine
    population's maximum, so no threshold recovers them. The rule looked finished for a day on
    an untaken measurement. Both halves of a separation must be re-measured, never one.

21. **`acos` cannot measure an angle that is supposed to be zero.** It loses half its bits
    there: `acos(clamp(a.dot(b)))` reads **1.5e-8 rad** on two unit vectors that agree to the
    last bit, and a gate whose refusal margin is 1e-9 refuses on that alone — it took
    `boss_plate` and `dome`, both previously untouched, at 2.98e-08 and 2.24e-08. Use
    `atan2(a.cross(b).norm(), a.dot(b))`, which is exact near zero and near pi both.

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
| 11 | "exact STL calls these valid", 2026-08-05 | reproducible, and the diagnosis drawn from it was still wrong. The OFF's six-figure precision is real, but the disagreement was **welded against unwelded**, not OFF against STL. Every claimed false red is invalid on exact STL across weld 1e-4…1e-12 and reads valid only at 1e-15, i.e. effectively unwelded — which is instrument #10 over again, since Manifold's output is 2-manifold by index construction and an unwelded reader cannot see a self-touch. Attributed to the format what belonged to the tolerance |
| 14 | **every reading on a seam-vertex model taken before `cab639ffd`** | `chainBulges` read `param(-1)` and `point(pos, -1)` on any chain whose bead overran a seam vertex, so the geometry was built partly from whatever the allocator had left before the buffer. `rib_into_boss` returned two topologies and three byte-orders from one unchanging binary. The largest instrument failure of the effort: it is not a script but the operator itself, and it silently contaminated an unknown share of the 225-model corpus |
| 12 | `sweep.sh --compare`, first version | compared face counts, but OFF writes polygons and STL triangles, so a plain cube reads `f=6` one way and `f=12` the other. Flagged 30 cells including cubes and buried the real signal |
| 13 | `sweep.sh` flakiness assertion, first version | asserted a 6-valid/2-invalid distribution over 3 runs, so the check failed itself about two times in five. An instrument for a random process needs its own power analysis |
| 10 | `bnd` as the D24 / A3 instrument | identically zero on every Manifold-backend export, by a parity argument, so it reads clean on a correct solid and on a broken one alike. `fillet-bench/README.md` gives the argument |
| 16 | **the §4d ball-seating instrument was left only in a session scratchpad**, 2026-08-05 | The scripts that produced §4d's 114 / 72 / 91 counts and its 9.8e-15-against-0.117 separation were never committed; they were recovered from `/private/tmp/claude-501/.../agent-ridge/`, which nothing preserves. They are now at `fillet-bench/work/coneseat/` together with `cone.py`, the port of the mesh-seated angular rule, whose self-proof is that it reads the closed form `R(1−tan(Δ/2)) − D` on `handblend_step` at every `$fn` from 8 to 48. An instrument that is not in the tree did not survive |
| 17 | **`sheet.sh` called `mesh.py` without the model's `comp` declaration**, 2026-08-06 | The A5 contact sheet ran the validity criterion with less context than `sweep.sh` gives it, and so got the answer wrong in *both* directions from one root cause. Before the criterion fix it green-lit `tee_small`, a model carrying a fully detached 0.35 × 0.10 × 0.40 mm fragment — a false acceptance sitting in the file the gate asks a person to review. After the fix it condemned `shallow_crease`, which renders two plates as its whole point and declares `// mesh.py-comp: 2` in its own source; the "smallest part" it flagged was an entire 120 × 40 × 34.8 mm plate. Fixed by reading the declaration the way `sweep.sh` does. **The lesson is not the missing flag but the shape:** two instruments that share a criterion must share its inputs, or the cheaper one silently answers a different question |
| 15 | **a validity verdict improving because the geometry went away**, 2026-08-05 | The local seat test took the sweep from **21 not-valid cells to 10 with zero regressions** — and every one of the 11 movers had lost 26–71% of its vertices. `cross` r=0.3 went 1519 → 435; `tee` at `$fn`=8 went to v=80 against a plain *unfilleted* solid of 64. 166 cells lost geometry and warnings rose 140 → 201. **The cells passed because the fillet stopped being built, and a dropped fillet is trivially a valid solid.** Every metric in `mesh.py` — `nonman`, χ, genus, `nmvert` — is monotonically happier the less geometry there is, so the whole criterion reads a false refusal as a success. The same run showed the second face of it: 11 `sweep.sh --selftest` checks "failed", each a known-defect assertion whose defect had merely vanished with the geometry that carried it. Instrument #6 is the ancestor — inertness read as a win — but this is worse, because #6 was byte-identical and visible, where this *moves every number in the right direction*. **Rule: no verdict improvement is real until the vertex count is quoted beside it**, and against a plain unfilleted render of the same model where that is the clearest statement |

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
