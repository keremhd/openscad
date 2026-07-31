# Code review — before the public PR

An outside read of the branch as it will actually be committed: `src/`, `tests/`
and the build files. `fillet-feature-design/` and `fillet-tests/` are scaffolding
and are judged only where CLEAN already asks a question about them.

Measured against `7b18d5aec` with a build made from a clean tree at that commit.

**Performance is not in this document.** The rounded tools are superquadratic in
time and memory; that has a repro and an acceptance test, so it is
[D14](remaining-work.md) — a defect — rather than a review comment.

---

## Blocking

Four of the five are this review's own: **R1, R2, R5 and R7**. R3 and R4 are one
open defect with an owner already — D12 — and R4 is downstream of R3 rather than
separate; they are written up here because a fold that leaves the output
self-touching is not something to open a public PR on, whoever fixes it.

### R1 — a diagnostic line is echoed on every invocation

The `message_group::Echo` mesh-statistics line at the top of `buildFilletTool`
goes out unconditionally: once per tool node, twice per `fillet()`. No other
OpenSCAD operator prints on success, and a model with a dozen fillets buries the
console in counts nobody asked for.

Gate it behind `debug=`, which already exists and already means "tell me what the
classifier saw". The warnings below it are conditional and should stay.

### R2 — on a build without Manifold, `fillet()` deletes the model

`GeometryEvaluator::visit(State&, const FilletNode&)`, the `#else` branch: the
children are collected, a warning is logged, and `geom` is left null. For the four
tool nodes that is correct — no backend, no tool solid. For `fillet()` it is not.
The wrapper's contract is that it returns the child with its edges blended, so
with no backend it should return the child. As written,
`fillet(r = 2) cube();` renders **nothing** on a CGAL-only build.

Two parts:

- Pass child 0 through when `node.type == FilletType::APPLY`.
- `tests/CMakeLists.txt` lists `render-cgal_`, `preview-cgal_` and
  `throwntogether-cgal_` disables for all four `*-tool-tests` under
  `if(NOT ENABLE_MANIFOLD)`, but not for `fillet-tests`. However R2 is resolved,
  that file needs either the three disables or a baseline that holds in both
  configurations. The `.csg` dump is fine either way — it comes from the preview
  path, where the node has already handed its child back.

*Read from the source; not reproduced, as there is no CGAL-only build here.*

### R3 — the blend folds back on itself, and D12 is not closed

**Not a new defect, and not a review finding — a second repro for D12.** It is
here because a fold that leaves the output self-touching is not something to open
a public PR on, whoever owns the fix.

**The composition is correct and must not be changed.** `fillet()` measures its
outer half against `blended`, and that is deliberate: the inner apex of an L is
exactly the case that needs it, where the reflex bead terminates on the very face
whose outline is being rounded. Two earlier drafts of this entry proposed keeping
the fillet pass's surfaces out of the classifier, and then excluding the bead's
tangency curves. Both are withdrawn, and D12 already contains the measurements
that kill them:

- The spurious edges split **zero** one-model-face-one-blend-face. They lie
  wholly inside the blend, so there is no tangency rim to exclude.
- They are folds, not slivers — 196 of 216 above 150 degrees on the tee. Grouping
  the bead's facets by tangency continuity cannot join facets 157 degrees apart,
  which D12 states in as many words.
- Threading "these surfaces are mine" from `fillet()` into the builder was
  implemented end to end, worked completely, and was **reverted on an invariant**:
  `round_tool` must return the same solid however its input was made. That
  paragraph in D12 is the standing answer and this entry does not reopen it.

#### What is new: the landed fix is geometry-dependent

D12 closed this by flooring the overshoot at the wall's own sagitta, measured on
its boss and its tee. That holds where it was measured and fails a short distance
away. Every row `$fn = 32`, a plain boss on a plate, counted on the blended solid:

| | features | > 150 deg | 4-face edges |
|---|---|---|---|
| D12's boss (r = 5 cyl, 24x24x4 plate), `r = 2` | 44 | **0** | **0** |
| tighter boss (r = 3 cyl, 40x40x6 plate), `r = 0.5` | 47 | 3 | 4 |
| the same, `r = 1` | 57 | **13** | **17** |
| the same, `r = 2` | 55 | 11 | 16 |

At `r = 1` the model alone gives 76 features and no folds; the blend gives 13
folds, and they are the same 13 edges as the spurious features (+4 convex, and 9
concave on a solid whose 32 concave edges were just filleted away).

**The folds and the non-manifold edges are one defect.** All 17 carry exactly
four faces — two sheets meeting along a line — at `z` 6.076 to 6.617 and radius
3.076 to 3.617 from the boss axis: mid-arc on the bead, not at either tangency
(`z = 6`, radius 3). R4 below is downstream of this, not separate.

On this single boss Manifold still reports genus 0, so the coincidence is
geometric rather than combinatorial — two sheets whose vertices land on each
other, which is what our merge-by-position then reads as a 4-face edge. On the
6 x 6 grid it becomes a real genus -1.

#### Symptom the user sees

A warning naming a crease they never drew, at a coordinate they cannot find:

```
WARNING: fillet: radius 1 does not fit 2 of the 110 crease(s) selected;
the worst is at [103.8, 30.07, 6.444] - another feature 0.709638 away
needs the same material.
```

`z = 6.444` is mid-arc on the bead, not on any surface in the model — the same
band as the folds above.

### R4 — the composed result is topologically degenerate

Same 6 x 6 model:

| | genus | connected components |
|---|---|---|
| `fillet(r = 1, outer = false)` | 0 | 1 |
| `fillet(r = 1, inner = false)` | 0 | 1 |
| `fillet(r = 1)` | **-1** | 1 |

Genus -1 over a single component means an Euler characteristic of 4, which no
closed orientable surface has: two shells meeting at a pinch. Neither half
produces it alone. Slicers and any Nef conversion downstream will object.

**Downstream of R3, and probably not a separate item.** The mechanism is the
folds: on the single boss they are two sheets of the bead meeting along a line,
four faces to an edge, and the blended solid there carries 286 such edges where
the model carries none. At one boss that stays geometric — Manifold still calls
it genus 0 — and on the grid it tips over into a genus the surface cannot have.

The test is cheap and worth running before anyone treats this as its own defect:
**fix the folds and re-measure the genus.** If it goes to 0, this entry closes
with R3 and needs no separate work. If it does not, the residue is a real second
cause and worth its own repro.

**Not D13's family — confirmed by measurement.** D13's fix (a ball at every seam
between two canal cells, `log-2026-07-31-d13.md`) changes nothing here: the boss
above gives 24, 35 and 5 four-face edges at `r = 0.5`, `1` and `2` with the fix
and without it, and the grid is genus -1 either way. So R3 and R4 are one open
defect and its owner is D12, which leaves R1, R2, R5 and R7 as the blockers that
are genuinely this review's.

### R5 — a scratch test is still in the suite

`FilletBuilder_test.cc`, `TEST_CASE("zzdebug rib", "[.]")`. Hidden behind a Catch2
tag, but it ships. Delete it.

### R7 — the comments are written in LLM register and have to be rewritten

**Decided, not open.** Between a third and two fifths of `FilletBuilder.cc` is
prose in a voice the rest of the tree does not use, and it does not ship in that
form.

Length is not the complaint. A dense geometry kernel earns long comments, and
several of these record a measurement that cost real time to obtain. The
complaint is the register:

- **It argues with the reader.** "the whole trick and is not optional", "'then'
  is the load-bearing word in it", "A right angle is exactly the wrong value, and
  measurably so". A comment states; it does not persuade.
- **It narrates how the answer was reached** rather than what the code does.
  Whole paragraphs are the shape of a debugging session — what was tried, what it
  left behind, what that looked like — with the conclusion at the end.
- **It restates the line below it in English.** Several comments are longer than
  the expression they precede and add nothing a reader of the expression lacks.
- **It addresses the reader in the second person** and asks rhetorical questions.

What to keep, compressed to a line or two each:

- the value of a non-obvious constant and why it is that value, not another
  (`kWallTurnDeg = 60`, the `1.5x` threshold, the tie margin);
- the failure a construction exists to avoid, stated as a fact — "a face resting
  exactly on a wall makes the boolean resolve two coincident surfaces";
- an invariant a caller must not break;
- a genuine surprise in the geometry or in Manifold's behaviour.

What to cut outright: rhetorical framing, the narrative of alternatives tried,
restatements of the code, and second-person address.

**The comment is not the archive.** The long-form reasoning is already preserved
in the commit messages, which are detailed and stay in history. Note that
`fillet-feature-design/` does *not* survive CLEAN, so anything worth keeping that
is too long for a comment belongs in a commit message, not in a doc that is about
to be deleted.

Target roughly a third of the current comment volume in `FilletBuilder.cc`, and
the same pass over `FilletBuilder_internal.h`, `FilletNode.cc` and the two test
files, which share the voice.

Landed commit messages are not being rewritten — that is history and not worth
the churn. New ones should use a plain descriptive subject line.

---

## Worth doing

### R6 — `FilletBuilder.cc` is 2518 lines

Roughly six separable pieces: mesh reduction and classification; chains and brush
selection; the size gate; the wedge tools; the rounded tools; the debug overlay.
`FilletBuilder_internal.h` already names the boundaries. A reviewer asked to read
this as one file will decline.

### R8 — the warnings are essays

Several run to three or four sentences and explain design rationale to the user;
the corner-coverage warning is about 70 words. OpenSCAD warnings are one line.
Keep the first sentence and the coordinates.

### R9 — small things

- `classifyEdge`: `aFar` is seeded with `A.v[0]` and replaced only if a vertex
  outside the edge is found, so a degenerate triangle silently answers "convex"
  instead of being rejected.
- `unionCells` skips `dropVolumelessParts` on the single-cell path and not
  otherwise. Harmless, inconsistent. (D14 may remove the question.)
- `epsAt` in `buildRoundSolid` reads `endSections[j.vert]` through
  `map::operator[]`, inserting empty vectors on a read path.
- `CMakeLists.txt`: `src/core/FilletNode.cc` sits between `CurveDiscretizer.cc`
  and `DrawingCallback.cc`, out of the alphabetical order the rest of
  `CORE_SOURCES` keeps.
- The `TEST_SOURCES` glob workaround is fine and honestly commented, but it
  documents that the geometry test layout wants fixing rather than working
  around it.

---

## What is already right

Recorded so that a later reader does not re-litigate it.

- The node wiring is a faithful copy of the `CgalAdvNode` pattern — `NodeVisitor`,
  `CSGTreeEvaluator`, `GeometryEvaluator`, `Builtins` — including the cache
  discipline, where a node's geometry is inserted at its *parent's* postfix by
  `collectChildren3D` rather than by the visitor.
- `CurveDiscretizer` is reused rather than reimplemented, and gains one small,
  well-motivated accessor.
- `Parameters::parse`, `LOG(message_group::...)`, `STR()`, the copyright header
  and the `$fn/$fa/$fs` plumbing are all idiomatic.
- 75 Catch2 cases across two files, guarded by `ENABLE_MANIFOLD`, driving the pure
  mesh combinatorics through an internal header instead of through the evaluator.
  That is a better testing story than most of `src/geometry/`.
- Regression baselines for all five renderers, with the two cases where the CGAL
  backend genuinely differs pinned to their own baselines and explained, rather
  than hidden from one backend.
- The API — four composable tool solids plus one wrapper, with selection brushes
  as ordinary CSG children so negative selection needs no new syntax — is well
  judged, and is the part of this that will age best.
