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

### R3 — the round pass re-reads the beads the fillet pass just built

`fillet()` measures its outer half against `blended` — the target with the inner
tool unioned in. That is deliberate, and D12 is why. What it also does is hand
the fillet tool's own surface to the classifier as if it were the user's model.

A plate with a 6 x 6 grid of bosses, `$fn = 32`, `r = 1`, every boss well inside
the plate. The two echo lines from the one `fillet()` call:

| | the user's model | `blended`, which is what the round pass is given |
|---|---|---|
| merged vertices | 2312 | 11686 |
| triangles | 4620 | 23672 |
| smooth surfaces | 37 | **1193** |
| non-manifold edges | **0** | **286** |
| convex feature edges | 1164 | 1302 |
| concave feature edges | 1152 | 122 |

Three things are wrong in that right-hand column. The surface grouping shatters —
37 walls become 1193, so `nearestOnWall` and the size gate are asking about
fragments rather than walls. 122 concave creases appear on a solid whose concave
creases were just filleted away. And a mesh that came out of a Manifold boolean,
which is 2-manifold by construction, is read back with **286 non-manifold edges**
— meaning `mergeMesh`'s exact-position merge is fusing bead vertices into pinch
points that the source mesh does not have. That last one is almost certainly the
cause of R4 below.

The user-visible symptom is a warning naming a crease they never drew, at a
coordinate they cannot find in their file:

```
WARNING: fillet: radius 1 does not fit 2 of the 110 crease(s) selected;
the worst is at [103.8, 30.07, 6.444] - another feature 0.709638 away
needs the same material.
```

`z = 6.444` is a height on the bead, not on any surface in the model.

T1 and the DOC section already record that an already-filleted mesh classifies
with spurious creases of both signs. This is that same effect reached from inside
a single `fillet()` call, where the user cannot opt out and `min_angle=` is the
only lever. At minimum the second pass should not offer surfaces the first pass
created as candidates.

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

The 286 non-manifold edges in R3's table are the likely mechanism, which makes
R3 and R4 one defect seen from two ends. Related to D13's family, but D13's repro
is two overlapping bosses and this is a plain grid, so confirm separately.

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
