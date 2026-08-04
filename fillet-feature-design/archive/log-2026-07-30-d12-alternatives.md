# D12 — the routes considered, and what was measured about each

**This is an exploration record, not a decision.** D12 was being implemented by
another agent while this was written, and nothing here describes what landed.
What it is for: five distinct routes were considered, four were rejected for
reasons that were *measured* rather than argued, and the measurements are worth
more than the conclusions — anyone reopening this will otherwise re-run them.

Everything below is `r = 2`, `$fn = 32` on the L bracket
`cube([10,30,26]); cube([26,30,12]);` unless it says otherwise.

---

## The defect, stated where it actually bites

Both tools are built from the *same* original child:

```
difference() { union() { child; fillet_tool(child); } round_tool(child); }
```

so the round tool is seated against walls the fillet tool has already changed.
Where a concave bead **runs out onto an open face**, its end cross-section is
left standing as a sharp crescent overhanging the rounded outline beside it.
`6.568` mm³ across the two ends at `$fn = 32`.

**Two things about the severity were got wrong at first and matter.**

- It is not confined to the perpendicular end faces. Decomposing the difference
  between the two compositions at `$fn = 64` gives 8.21 mm³ in 5 pieces, two of
  which run essentially the full 30 mm length.
- It is not cosmetic. It means `fillet()` cannot be used on an L bracket — the
  shape on the feature's own front page — without dropping to two tools and a
  brush. That is the operator failing its headline example, not a blemish.

There are really **two** defects, and this is the point that kills the cheap
fixes. Once the bead is unioned in, the outer pass both

1. **cuts a groove along an edge the bead has buried** — that edge is now interior
   to material, so cutting it is wrong; and
2. **never rounds the bead's new arc**, which sits at distance `r_inner` from the
   original crease.

Any fix has to stop doing (1) as well as start doing (2).

---

## Route A — run the outer pass on the already-filleted solid

The obvious composition. Fixes the bracket. Destroys anything with a curved blend
surface:

| model | as built today | outer pass on the filleted solid |
|---|---|---|
| cube | genus 0 | identical — no concave creases, nothing changes |
| boss on plate, blind bore | genus 0 | identical result |
| L bracket | genus 0, the lip | genus 0, **lip gone** |
| rib on plate | genus 0 | genus 0, a wash |
| pipe tee | genus 0, no warnings | **genus 31, 21 components, 31 warnings, 2963 mm³ gone** |
| two bosses | genus 0, no warnings | **genus 1, 33 warnings** |
| dome on plate | genus 0, no warnings | **11 warnings, beads dropped** |

Of the tee's 21 components, 20 are crumbs of 4e-6 mm³ and smaller.

**`min_angle` cannot separate the two cases.** Swept 18 / 30 / 45 / 60 on the
three curved models: the tee stays genus 12–31 with 31+ warnings throughout. The
creases being wrongly picked up are not low-angle slivers — they are the bead's
own surface at honest angles, read as a wall that wants rounding. The existing
threshold is derived from the *caller's* facet angle, but a bead's tessellation is
chosen by the builder, so on a curved spine its facets turn by more than
`1.5 x` the caller's facet angle. **Deriving the threshold from the mesh's own
tessellation was never tried and is the first thing to try** if this route is
revisited.

**Severity moves with `$fn`, which is its own warning.** At `$fn = 48` the tee
survives in one piece and it is the two bosses that shed crumbs, of 7e-6 and
9e-9 mm³. A failure mode that migrates between models as tessellation changes is
not a clean refusal. Test any candidate fix at several `$fn`.

### The concave counts are not spurious features — this was got backwards

Worth stating loudly because it inverted a conclusion. The classifier's `concave`
count on these models is **real creases**, not slivers:

| `$fn` | concave reported on the plain boss |
|---|---|
| 16 | 16 |
| 24 | 24 |
| 32 | 32 |
| 64 | 64 |

It tracks `$fn` exactly, because it is the boss's base ring — one genuine edge per
cylinder facet. The bracket's `concave = 1` is its one reflex crease. Those counts
falling under route A means only that the inner pass *filled* them.

Measured the right way round — on the **filleted** boss — the outer pass sees
`concave = 0` at `$fn` 16 and 24 and `concave = 1` at 32 and 64, and that lone
edge is a genuine tangential-contact sliver. **Route A introduces one spurious
feature on the boss where the current build has none.** It is marginally worse
there, not better.

---

## Route B — tell the outer pass which surfaces the inner pass created

Rejected on architecture, not on results. Threading provenance from `fillet()`
into the builder couples the two tools through the wrapper and makes the wrapper
privileged: it can then do something no caller composing the four tools can
reproduce.

**The invariant being protected:** the four `*_tool` modules are the composable
surface, and `round_tool(r) X` must be a pure function of `X` and `r`. A
`sibling_r =` parameter is the same coupling in a different costume — it also
makes a tool's output depend on something other than its own input.

---

## Route C — the outer pass extends its cut through the concave void

The idea: the bead lives in the concave *void*, which is empty in the child, so a
cut extended through it removes nothing when no bead is there and trims the bead
when one is. Inert when wrong, so no guessing needed — and `round_tool` already
sees the child's concave creases, so the voids are locatable.

**Rejected, and the reason is worth keeping.** The bead's new arc is at distance
`r_inner` from the original crease. With `r_inner = 5, r_outer = 1` the arc is
5 mm out and no `r_outer`-sized extension reaches it. Worse, extension cannot fix
defect (1) at all — that one needs the outer pass to *stop* cutting a buried edge,
which no amount of extra cutting achieves.

The bound that made this look plausible — "the lip can only exist where the outer
pass changed the surface, so it is confined to `r_outer`" — is true of the *proud
material* and false of the *defect*.

---

## Route D — the inner pass backs off by the outer's radius

The dual, and geometrically it does work: back the bead off and the buried edge is
un-buried (so cutting it is right again) and there is no new arc to round.

Rejected as a *parameter*, because the inner tool would need the sibling's radius
**and** the sibling's brush — `round_tool` may be brushed to only some creases, and
backing off where the outer pass does not cut leaves a gap for no reason — **and**
the sibling's profile, since a bevel's setback is not a radius. It also changes
standalone `fillet_tool` output, so it cannot be free the way an inert extension
is.

But the idea is right, and it survives in route E.

---

## Route E — the outer tool's own solid, used as a negative brush

**This works, needs no new parameter, no coupling, and no builder change.**
`round_tool(ro) child` *is* a solid describing exactly the region the outer pass
will remove, so it can be handed to the inner tool as a negative brush:

```openscad
module WORLD() { translate([-100,-100,-100]) cube([300,300,300]); }  // any solid enclosing the model

difference() {
    union() {
        m();
        fillet_tool(r = ri) {
            m();
            difference() { WORLD(); round_tool(r = ro) m(); }
        }
    }
    round_tool(r = ro) m();
}
```

Both tools still see only the original child. The composition does the work.

**Measured, `r = 2`, `$fn = 32` — all genus 0, no warnings, one component:**

| model | vs today |
|---|---|
| pipe tee, dome, boss, blind bore | **byte-identical** |
| two bosses | −5.3 mm³ |
| L bracket | −3.4 mm³ — the lip |

**Cost: +1.5%** (753 ms against 742 on a boss at `$fn = 128`). The second
`round_tool` is nested deeper than the first, so it hits the geometry cache rather
than recomputing; what is paid is one extra boolean.

Three properties that make it worth writing down rather than merely working:

- **It self-corrects for brushes.** The brush region *is* the outer tool's own
  solid, so if the outer pass is itself brushed, the bead backs off only where the
  outer pass actually cuts. This is what route D could not manage.
- **It is inert where it does not apply.** Closed beads never enter the outer
  tool's region, which is why the tee and dome come back byte-identical.
- **The bead ends flat**, not blended into the round. Accepted deliberately — a
  three-way blend where a concave blend meets a convex one has no closed form and
  is the same class of problem as corner cells.

### The enclosing solid

`hull()` works and removes the magic number, and there is a theorem behind it:
**a concave crease is provably strictly inside the convex hull**, because at a
reflex edge the solid spans more than a half-space, so no supporting plane can
touch it. Crease selection is therefore never what breaks.

Identical to the box brush on all six models, and at every radius tried —
including radii where the *bead* overruns the hull, which does not matter because
the brush governs selection and the crease stays inside:

| r | box brush | hull brush | |
|---|---|---|---|
| 2 | 13289.4036 | 13289.4036 | identical |
| 3 | 12960.9744 | 12960.9744 | identical |
| 4 | 12512.4568 | 12512.4568 | identical |

Costs, on the bracket:

| region | ms |
|---|---|
| `hull() m()` | 57 |
| `minkowski() { hull() m(); cube(4, center = true); }` | **57** |
| `minkowski() { hull() m(); sphere(2); }` | 65 |
| union of 6 translated hulls | 58 |

Minkowski of a hull with a cube is **free** — both operands are convex, the case
Minkowski is fast at. The 6-translate inflation saves nothing and costs
readability; drop it.

**The conclusion is to stop solving this in SCAD.** If route E ever becomes the
node's default, the node already has the target's bounding box — inflate it by `r`
and there is no hull, no Minkowski and no magic number, because the enclosing
solid is never expressed in SCAD at all. The complexity exists only in the
*documented equivalent*, where a generous box plus one line about why a larger
brush is always safe is the honest version, with `hull()` as a parenthetical.

**Residual risk, not reproduced:** the corner-coverage rule is the mechanism that
could still bite — a junction whose setback region near the hull boundary is not
covered would have its corner dropped. No model was found that does it. If one
appears, `minkowski(hull, cube(2r))` is the fix and costs nothing.

### If route E were made the default

In favour: `fillet()` stays sugar, which is the property the provenance route
spends; provably inert where it does not apply; fixes the bracket; ~1.5%.

Check first, none of these were tested:

1. **Other profile combinations** — everything above is `fillet_tool` +
   `round_tool`. `chamfer_tool` inside with `bevel_tool` outside has the same
   junction, and a bevel's setback is not a radius, so the brush region differs.
2. **A brush on `fillet()` itself** — the inner tool would then carry two brushes
   intersected.
3. **It changes existing output** — the bracket moves by 3.4 mm³. A bug fix, but a
   behaviour change that belongs in the release note.

---

## The SCAD equivalent — what is and is not writable

**A nested module cannot see the outer module's children, and fails silently.**

```openscad
module blended() { union() { children(); fillet_tool(r = r) children(); } }
difference() { blended(); round_tool(r = r) blended(); }     // builds NOTHING
```

`children()` inside `blended()` is *its own*, and it has none, so both branches are
empty with no error and no warning. The `.csg` dump is where it shows: every
`children()` comes out a bare `group()`. This is the first thing a reader will
try, so it deserves a doc line.

**Forwarding at the call site fixes it**, and is exact — same triangle count and
the same `13286.2607` mm³ as the composition it mirrors:

```openscad
difference() {
    blended() children();
    round_tool(r = r) blended() children();
}
```

---

## Caching — measured, because two claims about it were wrong before they were right

`smartCacheInsert` is called from `collectChildren*`, so a node's geometry enters
the cache at its **parent's** postfix, once every sibling has been traversed.
Consequences:

- **Duplicated top-level siblings never dedupe.** Two identical `fillet_tool`
  calls side by side cost what two different ones cost — 364 ms vs 361 ms, against
  210 ms for one. Two identical plain unions likewise: 322 vs 327, against 204.
- **Deeper shared subtrees do hit.** This is why the doubled composition costs one
  boolean rather than one blend.

**A duplicated sibling can be made to dedupe by adding one level above the first
occurrence**, giving it a parent that finishes — and inserts — before the second is
reached. On a deliberately expensive union of two `$fn = 200` spheres:

| | ms |
|---|---|
| the union once | 204 |
| the same union twice, as siblings | 322 |
| twice, extra `union()` around the first | **214** |
| twice, extra `group()` around the first | 215 |

Worth knowing as a general OpenSCAD technique. **It buys nothing in the fillet
composition**, because the duplicated union is cheap — breakdown on a boss at
`$fn = 128`:

| | ms |
|---|---|
| the boss alone | 56 |
| `fillet_tool` on it | 314 |
| `union` of the two | 332 — so the boolean is ~18 ms |
| `round_tool` on the plain boss | 488 |
| `round_tool` on the filleted boss | 767 — the round pass costs the same either way |

18 ms out of 780 is why the wrapper measured 781 against 780. Reach for it when
the shared subtree is expensive, not by reflex.

**Nested module vs repeated inline expression makes no difference to any of it** —
instantiation is inlined into the node tree, so both give the same tree bar
`group()` wrappers, the same cache keys, and the same time to the millisecond
(88 ms each). The submodule buys readability, nothing else.

---

## API brainstorm — profiles and sizes per side

Not part of D12, but it interacts with it and was worked through.

**The gap is profile selection, not combination selection.** `inner =` / `outer =`
already give fillet-only, round-only and both. What cannot be said today is *flat
instead of round on a given side*.

**An enum over combinations was rejected.** The choice is a product —
inner ∈ {none, round, flat} × outer ∈ {none, round, flat} — and an enum flattens a
product into 8 invented names. Worse, the names collide with this codebase's own
vocabulary, where `chamfer_tool` is specifically the *inner* flat and
`bevel_tool` the *outer* flat, so "fillet and chamfer" reads as two profiles on
one side.

**Let the parameter name carry side and profile, and the value carry size.** Four
parameters mapping 1:1 onto the four existing tools — `r_inner`→`fillet_tool`,
`t_inner`→`chamfer_tool`, `r_outer`→`round_tool`, `t_outer`→`bevel_tool` — with
bare `r` / `t` as the both-sides shorthand, and one rule: a side given both is an
error.

**`r1` / `r2` was rejected specifically.** `cylinder(r1, r2)` is a *gradient* — the
surface interpolates between them. Inner/outer is categorical, nothing
interpolates. And on a blend operator `r1`/`r2` is exactly what a reader will read
as *radius varying along the edge*, which all three drafts explicitly disclaim as a
non-goal (`doc-page/fillet.md`, `pr.md`, the wiki caveat). If variable radius is
ever built, `r1`/`r2` is the name it will want.

Tested module, on the bracket at `$fn = 32`, all single-component, no warnings:

| call | tris | volume |
|---|---|---|
| `fillet(r = 2)` | 2748 | 13286.2607 — exactly the chained composition |
| `fillet(r_outer = 2)` | 2492 | 13266.5335 — exactly today's `difference(child, round_tool(child))` |
| `fillet(r_inner = 3, t_outer = 1)` | 200 | 13448.5118 |
| `fillet(t = 1.5)` | 116 | 13218.5359 |
| `fillet(r_inner = 4, r_outer = 1)` | 2776 | 13589.0779 |

`fillet(r_inner = 2, t_inner = 1)` aborts on the assert.

Two properties: **it degrades to today's behaviour for free** — with no inner tool
`blended()` *is* `children()`, so outer-only is byte-identical and needs no special
case; and **flat profiles are far cheaper**, 116 triangles against 2748, which is
its own argument for exposing the axis.

**Independent sizes are not a builder risk.** Each tool is an ordinary invocation
on its own input; a bug there would already show at equal sizes. The only nuance is
that once the passes are chained, `r_inner` decides what the outer pass *sees* —
a different input, not a new class of input.

---

## Measurement traps that cost real time

- **Do not difference two near-coincident solids to measure their disagreement.**
  `A - B` on the bracket at `$fn = 64` produced 40–60 shards of ~0 mm³, and the
  `5:1` and `1:5` runs returned byte-identical numbers from a stale STL. Those
  figures are worthless. Render and look, or measure a specific quantity.
- **Never use bounding boxes for anything about brush extent.** A bead's
  cross-section is `r` across whatever the brush is, so a bbox cannot tell a 1 mm
  bead from a 6 mm one. Use volume.
- **Check connected-component counts on figures, not just the picture.** A figure
  that cuts a model open leaves bead ends floating if its cutting box is not
  generous, which looks exactly like the tool shedding crumbs and is not.
- **`--viewall` fits the model to whatever canvas it is given**, so `--imgsize` is
  part of the figure. Re-rendering a set at one "documented" size letterboxes the
  ones composed at another.
