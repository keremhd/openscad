# 2026-07-28 — what happens when the radius doesn't fit

§6.5 had said "clamp or warn" since the plan was written, and nothing forced the
choice until the test suite needed to state an expected outcome. **Decision: warn
and drop the chain. No clamping, no automatic size variation.**

## How it surfaced

Reading `case_inner_corner_fillet`, the large variant asks for r = 35 on an L
whose two faces are 30 mm long. The hand reference builds the bead anyway, so it
spans x,y ∈ [10,45] while the arms end at 40 — 5 mm of material hanging past the
end of both faces, plainly visible in the render's second column. The variant
carried `has_ref = false`, so no check ever looked at it, but the picture drew it
in the same green that means "known-good answer" everywhere else in the suite.

That was the tell. `has_ref = false` was doing two unrelated jobs: "no closed form
exists" (the junction corners, where the equal-radius blend has no elementary
solution) and "we haven't decided what should happen" (the oversized radii). The
first is a fact about geometry. The second was an open decision hiding in a
boolean.

## Why not clamp

Clamping does not stay local. Clamp edge A to fit its tight vertex, and the blend
where A meets B now has two radii to reconcile — so either B clamps too, or the
corner is undefined. At the fixed point the `min` has run over the whole connected
network of chains: one tight vertex in a corner of the part silently shrinks a
fillet on the opposite side, and nudging that one dimension by 0.1 mm resizes
geometry nobody was looking at. §9 already lists discontinuous neighbour-set
membership as a hazard; network-wide clamping adds nonlocality on top, which is
the worse half — a discontinuity you can see beats one you can't.

"Clamp to the largest radius that fits" also assumes that radius is cheap to
know. Two of §6.5's three constraints are local; "half the distance to the nearest
other feature" is global and circular, since what counts as nearest depends on how
far the tool reaches, which depends on the radius being solved for. Computing the
true maximum costs about what building the tool and testing it costs. Clamping
doesn't skip that work, it hides it — and then hides the result.

Discard-rather-than-clamp is also what §6.3.1 already does with infeasible triplet
solutions at corners, so the operator now has one policy instead of two.

Variable radius along a chain stays on the table as an explicit feature — radii
per chain vertex, interpolated, tangency matched at joins. It is a bad *fallback*
for the same reason it is a good feature: the user gets geometry they didn't ask
for, and can't see that it's wrong.

One exception, for float noise rather than intent: exceeding a limit by less than
`eps` clamps silently. Dropping a fillet over 1e-9 would be its own bug.

## What changed in the suite

The variant tuple's third field stopped being a boolean and became a kind:

| kind | means | checks |
|---|---|---|
| `ref` | a hand-written answer exists | `tool` `sandwich` `emits` |
| `drop` | out of range — the answer is a warning and no tool | `drops` `sandwich` |
| `none` | no closed form to compare against | `sandwich` `emits` |

Five variants moved from reference-free to `drop`: the large variants of
`inner_corner_fillet`, `inner_corner_chamfer`, `outer_edge_round`,
`outer_edge_bevel` and `hole_mouth_round`. They gained a real check instead of
being excused from one. The four junction cases are the only `none` left.

`drops` is the first check that reads the log and not just the geometry, and it
has to be: an empty result is exactly what a deliberate refusal and an
unimplemented operator have in common. Emptiness alone would let the second pass
as the first, so the check wants the empty tool *and* a matching warning. The
regex lives in `lib/_common.sh` and is deliberately loose — enough to prove the
warning is about a fillet size, not so tight that rewording the message turns a
correct operator red.

The picture stopped drawing columns 2, 4 and 6 for `drop` and `none` variants.
Those are the reference columns, and a variant with no reference has nothing to
put in them; the diff column in particular was reporting disagreement with a shape
that was never the answer, while `check.sh` ran no `tool` check behind it. That
contradicted the README's own claim that an empty red column and a green test are
the same fact. It also drops the row's only expensive computation.

One thing deliberately *not* done: none of this went into `expectations.txt`. That
file is a ledger of temporary debt — "red today, delete the line when the
milestone lands." Warn-and-drop is permanent behaviour, so it belongs in the case
files as a declared kind. Written as an expectation it would read as "not there
yet", and someone would eventually delete it thinking they'd fixed something.

## Where it stands

Nothing validates size against the feature yet, so every `drops` check is red —
recorded in `expectations.txt` as debt. The two wedge tools get there by building
a tool they should have refused; the rounded ones by emitting nothing and staying
silent about it. Both are the right kind of red: the check now describes what the
operator must eventually do, rather than declining to have an opinion.
