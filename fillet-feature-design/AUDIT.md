> **HISTORICAL — describes the OLD artifact being replaced.** An accurate audit of the swept-tool
> implementation (332/353 cells, the 21 junction failures) — useful to understand what the rewrite
> deletes and why, not what to build. Current spec: [START-HERE.md](START-HERE.md) →
> [REQUIREMENTS.md](REQUIREMENTS.md).

# Audit: what is actually built

**2026-08-06. Tree `e44d25070`, `src/` clean.**

An independent description of the fillet artifact, produced by reading the source and
re-taking the measurements. **Nothing here is cited from `STATE.md`, `TRAPS.md`,
`ACCEPTANCE.md` or the archive.** Every claim is tied to a source line I read or a number I
measured today. Where this file and the narrative record disagree, the disagreement is listed
in §6 as a finding, and this file is the one to believe — not because the record is careless,
but because it is a log of a moving target and this is a description of a fixed one.

It exists because the record had grown to ~3,300 lines across four documents plus an archive,
and no longer answered the question "what is in the tree".

---

## 1. Trust anchors

All four re-established today, in this order, on this tree.

| anchor | value | how established |
|---|---|---|
| tree | `e44d25070`, working tree clean | `git status --porcelain` → 0 files |
| shipping binary | md5 **`fd3dec78`**, 2026-08-06 16:54 | rebuilt `OpenSCADExe`; make found nothing to do, binary unchanged |
| unit suite | **2230 assertions / 88 cases, all pass** | rebuilt `OpenSCADUnitTests` (mtime 23:47), ran it |
| bench | **332 valid / 21 invalid of 353 cells** | `results/sweep-fd3dec78.tsv`, verified single-binary |

**The load-bearing anchor is the second one.** The current tree builds to `fd3dec78`, which is
the same binary the 353-cell sweep in `results/` was taken on — every row of that file carries
`fd3dec78` in its `bin` column, with no other value present. So the recorded sweep describes
*this* tree, and did not need re-running. That is the single fact that makes the rest of this
document cheap to produce and safe to trust.

Scope of what is verified: Release build, Manifold backend, macOS arm64, this machine. No CGAL
arm, no other platform.

---

## 2. What is built

The pipeline, read off `FilletBuilder.cc:3230` (`buildFilletTool`) and the functions it calls.
Four SCAD modules (`fillet_tool`, `round_tool`, `chamfer_tool`, `bevel_tool`) all enter here;
`type` selects concavity and whether the tool is a wedge. `fillet()` is a fifth entry
(`FilletType::APPLY`) handled one level up, in `GeometryEvaluator.cc:1036`.

1. **Merge** — `mergeMesh` (`:3242`) welds MeshGL vertices by exact position, because raw
   indices do not give topological adjacency. `buildEdgeAdjacency` (`:3243`) rebuilds
   edge → incident-triangle.
2. **Classify** — `classifyEdges` (`:3254`), dihedral + concavity per two-face edge.
   Threshold is `node.min_angle` if given, else the constant `46.0` (`:3247`).
3. **Select** — `selectedEdges` (`:3271`) keeps feature edges matching the tool's sign.
4. **Chain** — `buildChains` (`:3272`) walks selected edges into spines; degree ≠ 2 vertices
   terminate a chain, so junctions are chain ends by construction.
5. **Resample** — `resampleChains` (`:3275`) evens out station spacing, before the brushes are
   consulted, so there is only one chain-parameter space.
6. **Brush** — `chainSelection` intersects the spine (not the tool volume) with the brush
   volume, giving keep-intervals in chain parameter.
7. **Size gate** — `checkChainSizes` (`:1020`) refuses a whole chain on `OffFace` (the seated
   ball hangs off the end of a wall) or `Crowded` (a neighbour's contact sits in this bead's
   material).
8. **Section** — `wedgeSections` (`:1900`) for chamfer/bevel; `roundSections` (`:2056`) for
   fillet/round, which emits a pentagon `w` plus the arc region `u` to be cut out of it.
9. **Assemble** — `appendChainCells` hulls consecutive section pairs into cells;
   `appendSeamCovers` (`:1678`) adds a third solid at each bend; `cornerCell` (`:2522`) builds
   a junction cell; `unionCells` (`:1371`) unions, capped at 32 survivors.
10. **Emit** — the tool solid. The caller unions it (concave) or subtracts it (convex).

For `fillet()`, `GeometryEvaluator.cc:1044-1059` composes:
`difference(union(target, fillet_tool(target)), round_tool(blended))`. Note the argument to
the second call is **`blended`**, not `target` — the convex pass runs against the solid the
concave pass produced. The comment at `:1037` states the reason (a bead's runout leaves a
crescent standing in a face, which a round pass that never saw the bead would leave as a lip).

---

## 3. The 21 failing cells

From `sweep-fd3dec78.tsv`. Taxonomy assigned by me from the metric columns, not from any
prior triage.

| failure kind | cells | signature |
|---|---|---|
| non-manifold edges | **12** | `nonman > 0` |
| pinched vertex | **5** | `nmvert > 0`, `nonman = 0` |
| detached fragment | **4** | `comp > wantcomp`, `nonman = 0`, `nmvert = 0` |

| model | fn | r | kind | detail |
|---|---|---|---|---|
| `tee` | def | 0.5 | pinched | nmvert=2, comp=3 |
| `tee` | def | 0.9 | pinched | nmvert=1, comp=2 |
| `tee_oblique` | def | 0.2 / 0.3 / 0.8 | pinched | nmvert=1, comp=2 each |
| `tee_small` | def / 10 / def | def / def / 1.5 | detached | comp=2 |
| `cross` | def | 0.3 | nonman | nonman=1 |
| `cross` | def | 0.9 | detached | comp=2 |
| `boss_plate` | 8 | def | nonman | nonman=8 |
| `two_bosses` | 8 | def | nonman | nonman=6 |
| `pipe_into_face` | 8 | def | nonman | nonman=2 |
| `dome` | 8 | def | nonman | nonman=4 |
| `hole_plate` | 8 | def | nonman | nonman=8 |
| `rib_into_boss` | 14 / 32 | def | nonman | nonman=3 / 4 |
| `refused_neighbour` | def | 0.2 / 0.8 / 0.9 / 1.0 | nonman | nonman=1 each |

**The `$fn`=8 family is not a `$fn`=8 problem.** 13 cells run at `$fn`=8 and **8 of them are
valid** — `tee`, `tee_oblique`, `tee_small`, `tee_large`, `cross`, `bevel_boss`, `cyl_control`,
`rib_into_boss`. The 5 that fail are exactly the models where a *curved surface meets a flat
plate*: `boss_plate`, `two_bosses`, `pipe_into_face`, `dome`, `hole_plate`. Cylinder-meets-
cylinder passes at the same tessellation. That is a sharper statement than "the `$fn`=8 boss
family" and it points at the curved-to-flat contact, not at `$fn`=8 as such.

**All 21 are at a junction or a contact between two blended features.** No cell fails on a
single isolated crease.

---

## 4. Mechanism inventory

Every tunable constant in the builder, with its site. These are what "patchwork" refers to:
each was added to treat a symptom, and none is covered by a direct unit test.

| constant | value | site | what it treats |
|---|---|---|---|
| `kDefaultCreaseThresholdDeg` | 46.0 | `_internal.h:131` | crease vs tessellation seam |
| `kSliverFraction` | 0.5 | `_internal.h:318` | when to resample a chain |
| `kWallTurnDeg` | 60.0 | `.cc:753` | turn cap on the wall walk |
| `kMaxUnitedParts` | 32 | `.cc:1329` | union blowup (reached 17.5 GB) |
| `kMinCross` / `kOnFace` | 0.1 / 1e-6 | `.cc:2238-9` | corner-cell degeneracy |
| `seamOver` | 0.10·r | `.cc:2589` | bead overrun past a seam vertex |
| `kSamples` | 6 | `.cc:2772` | sampling density |
| `kCoplanarDeg` | 1.0 | `.cc:3173` | skip seam cover when collinear |
| `kMaxNamedRefusals` | 24 | `.cc:3455` | warning volume (cosmetic) |
| eps ladder | `max(1e-3·r, 1e-9)` | `.cc:1911, 2063, 2554` | tool must cross walls transversally |

**Test coverage is the finding here.** Of 58 functions defined in `FilletBuilder.cc`, **38 have
zero direct calls from either test file.** They are reached only end-to-end through
`buildRoundSolid` / `buildWedgeSolid`. The uncovered set is almost exactly the assembly path
and the compensating patches — and it includes **`buildFilletTool` itself**, the top-level
entry every one of the four SCAD modules goes through, which has no direct unit test at all:

`buildFilletTool`, `appendChainCells`, `appendSeamCovers`, `cornerCell`, `cornerProfile`, `chainBulges`,
`chainNormals`, `wallOvershoot`, `seamRoom`, `overhang`, `truncationParam`, `unionCells`,
`dropVolumelessParts`, `composeParts`, `endTouched`, `endWindow`, `endAnchored`,
`creaseLeavesUnfilleted`, `makeRoundSection`, `pentagonSection`, `sectionAnchor`,
`sectionNormal`, `sectionClearance`, `lerpSection`, `toSectionSpace`, `rawStationChain`,
`inradius`, `hullPoints`, `filletedEdges`, `wallsAway`, `sidedTris`, `intervalCovers`,
`pointSegmentDistance`, `closestPointOnTriangle`, `markerHalf`, `addBoxMarker`,
`addCubeMarker`, `roundSections`.

**This is the concrete reason the artifact is hard to trust.** Not that it is wrong — the bench
says 332/353 — but that for most of these there is no way to state what the mechanism is *for*
except by removing it and measuring. That is §7.

---

## 5. What is *not* dead

A deliberate negative result, because it constrains how much simplification is available.

- **No orphaned functions.** All 58 definitions in `FilletBuilder.cc` have at least one caller.
- **No unread constants.** Every constant in §4 has a live read site.
- Volume is `FilletBuilder.cc` 3,525 lines = 2,260 code + 957 comment (27%) + 308 blank;
  `FilletBuilder_internal.h` 642 lines = 228 code + 358 comment (55%).

So the simplification available is **not** dead-code removal. It is either (a) comment volume,
which the S2 commits have been working on, or (b) mechanisms that are live but no longer
*load-bearing* — which is unknown until measured.

---

## 6. Corrections to the record

Found by reading source, listed because each contradicts something in the narrative docs.

1. **The 46° constant is read at 6 production sites, not 3.** `FilletBuilder.cc:151`
   (`classifyEdges`), `:247` (`selectedEdges`), `:697` (`smoothSurfaces`), `:1300`
   (`wallOvershoot`'s walk), `:2202` (`creaseLeavesUnfilleted`), `:3185` (debug colouring).
   The redesign note names three. Five are functional; the sixth is debug-only.

2. **Face provenance is already plumbed, and is deliberately not used to reject.**
   `Tri::originalID` (`_internal.h:51`) carries the source-surface id Manifold propagates
   through booleans; `classifyEdges` takes a `useProvenance` flag, set at `:3251` when the mesh
   has more than one source id. The header states outright: "provenance is reported, not used
   to reject." Any proposal to classify creases by provenance has already been considered and
   declined here — correctly, since it would make a tool's behaviour depend on how its input
   was built rather than on the input geometry, which the four modules cannot do and stay
   composable.

3. **The `$fn`=8 failures are curved-meets-flat-plate, not a boss family** (§3).

4. **Trap 1 fired during this audit.** `OpenSCADUnitTests` was dated 2026-08-05 23:23 against a
   `FilletBuilder.cc` of 2026-08-06 00:29 — the test binary was an hour older than the source
   it tested. The 2230/88 figure quoted in the record was taken on that stale binary. Rebuilt
   and re-run here; it still passes, so nothing was hidden, but the check was not valid as
   taken.

---

## 7. What is still unknown

One question, and it is the one that matters for both trust and simplification:

> **For each mechanism in §4, does removing it change any output?**

None of them can currently be answered from the tree, because 37 of 58 functions have no direct
test and the constants have no coverage at all. The method is mechanical:

1. Disable one mechanism (constant to a no-op value, or early-return the function).
2. Rebuild; **verify the binary's mtime moved** before believing anything (see §6.4).
3. Run the 353-cell sweep, 3 renders per cell, one binary.
4. Diff verdicts *and vertex counts* against `sweep-fd3dec78.tsv`.

Vertex counts are not optional: a mechanism whose removal makes cells pass by causing the bead
to not be built at all would otherwise read as a fix. A verdict improvement with a large vertex
loss is inertness, not repair.

Expected outcomes per mechanism: **inert** (no cell moves — delete it), **load-bearing** (cells
regress — keep it, and the diff is the one-line statement of what it is for, which is the
documentation that does not currently exist), or **harmful** (cells improve — a finding).

Cost is roughly one sweep per mechanism. Ten mechanisms in §4, so this is a bounded grind, not
an open-ended investigation, and every outcome is useful regardless of direction.

---

## 8. Standing of the other documents

`STATE.md`, `TRAPS.md` and the archive remain accurate as history and are the record of *why*
decisions were made. They are not a description of the artifact and should not be read as one.
`TRAPS.md`'s durable content is roughly six environment facts (this machine's locale, ASan,
the build watchdog, ccache, BSD sed, python tooling) plus three measurement rules (state the
weld tolerance, quote vertex counts beside verdicts, run a new instrument on a known answer).
The rest is archaeology.

`fillet-bench/` is the asset. `sweep.sh`, `mesh.py` and the 28 models are an implementation-
independent validity oracle and are what makes §7 possible at all.
