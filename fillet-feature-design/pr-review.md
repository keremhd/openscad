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

### R3 — the round pass cannot tell a bead's tangency from a crease

**The composition is not the bug and must not be changed.** `fillet()` measures
its outer half against `blended` — the target with the inner tool unioned in —
and that is deliberate: D12 is why, and the inner apex of an L is the case that
needs it. The reflex bead terminates on the end face whose outline is being
rounded, and a round pass that cannot see the bead has nothing to blend the two
into each other. An earlier draft of this entry proposed keeping the fillet
pass's surfaces out of the classifier; that would break exactly this and is
withdrawn.

What is wrong is narrower: **a bead meets both its walls tangentially, and the
classifier reads a feature edge along that contact anyway.**

A plate with a 6 x 6 grid of bosses, `$fn = 32`, `r = 1`, every boss well inside
the plate. This model is the discriminator, because **every concave chain on it
is a closed ring** — 1152 selected concave edges is 36 bosses x 32 segments
exactly, so no bead has an end and no bead runs out onto any face. D12's
mechanism has nothing to do here, and the round pass should therefore see the
same convex feature set on `blended` as on the model. It does not:

| | the user's model | `blended`, which the round pass is given |
|---|---|---|
| merged vertices | 2312 | 11686 |
| triangles | 4620 | 23672 |
| distinct source ids | 37 | 1193 |
| non-manifold edges | **0** | **286** |
| convex feature edges | 1164 | **1302** (+138) |
| concave feature edges | 1152 | **122** (should be ~0) |

260 feature edges that cannot be D12's: 138 convex ones the model does not have,
and 122 concave ones on a solid whose concave creases were just filleted away.
Both sit on the bead's tangency lines, where the dihedral is zero by construction
and whatever the classifier reads is tessellation noise.

The distinct-source-id row is *not* a symptom — it is high because the tool is
batch-unioned from about that many hulls, each carrying its own id. It is in the
table only because it is on the same echo line. The non-manifold row belongs to
R4, not here.

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
only lever.

#### The route: exclude the tangency curves, which the first pass already has

A fillet meets its walls tangentially — that is what a fillet is — so **no edge
lying on a bead's contact line is ever a feature edge, at any tessellation**. The
contact is C1 by construction, and the fillet pass has already computed every one
of those curves: they are the `TA`/`TB` contact points along each chain, in
`ChainContact`. A union does not move geometry, so those coordinates are still
valid in `blended`. Carry them out of the fillet pass, hand them to the round
pass, and reject any candidate edge whose endpoints lie on one, tested against a
spatial hash.

It is precise about what it keeps: the runout lip D12 is about is a bead **end
cap** outline, not a tangency line, so it survives untouched. And it needs no new
machinery — only plumbing a result the first pass already produces into the
second.

Alternatives considered, and why they rank below it:

- **Provenance.** `runOriginalID` is already read, and within `fillet()` both
  operands are ours, so "which faces are the tool's" is free — genuinely
  different from cancelled M11, which was a general re-fillet rule over arbitrary
  user input. But it does not fix this case: a spurious tangency edge has one
  tool face and one model face, so a both-faces-are-tool test skips it.
  Complementary at best.
- **Triangle quality.** Already closed by measurement — see Cancelled, where the
  two populations came out inverted.
- **Classify the original and add the bead ends explicitly** as extra chains,
  since the fillet pass knows where each chain terminated. The most surgical
  reading of what D12 actually needs, but it is real new machinery and has to
  reconstruct the lip outline rather than read it off the mesh.

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

The mechanism is coincident geometry at the bead's tangency: a mesh that came out
of a Manifold boolean, and is 2-manifold by construction, is read back from
`blended` with **286 non-manifold edges** where the model had none — so
`mergeMesh`'s exact-position merge is fusing bead vertices into pinch points the
source mesh does not have. Whether the pinch is only in our reading or in the
output too is the first thing to settle: the genus is Manifold's own verdict on
the *result*, so at least one of the two is real geometry.

Note this is **not** fixed by R3's route. Excluding the tangency curves stops the
classifier inventing creases there; it does not stop the vertices coinciding.
R3 and R4 share a cause but need separate fixes. Related to D13's family too, but
D13's repro is two overlapping bosses and this is a plain grid, so confirm
separately.

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
