# Fillets, rounds, chamfers and bevels

*Draft for the public OpenSCAD wiki. This directory does not survive the PR
merge; the text is meant to be pasted into the wiki, and the images uploaded
alongside it.*

Rounding an edge in OpenSCAD has always meant building the round yourself —
`minkowski()` with a sphere, or `hull()` over spheres placed at the corners, or a
`rotate_extrude()` profile subtracted by hand. Each works, each is slow or
fiddly, and none of them lets you say *"this edge, not that one"* without
rebuilding the model around the answer.

These operators take a solid and find its edges themselves.

```openscad
fillet(r = 3) my_model();
```

![Before and after](fig-quickstart.png)

*Left: the model as written. Right: the same model wrapped in `fillet()`.*

It works on anything that is a solid — a CSG tree, an imported STL, the result
of a `hull()`. Nothing has to be authored in a special way, and no edge list is
maintained by hand.

## The five modules

`fillet()` is sugar. Underneath are four modules that each build a **tool
solid** — the material a blend adds or removes — which you compose yourself:

| Module | Blend | Edges it follows | How you use it |
|---|---|---|---|
| `fillet_tool(r)` | round | concave (inner) | `union()` it in |
| `chamfer_tool(t)` | flat | concave (inner) | `union()` it in |
| `round_tool(r)` | round | convex (outer) | `difference()` it out |
| `bevel_tool(t)` | flat | convex (outer) | `difference()` it out |

and `fillet(r)` is exactly:

```openscad
module fillet(r = 2, inner = true, outer = true) {
    difference() {
        union() {
            children(0);
            if (inner) fillet_tool(r = r) children();
        }
        if (outer) round_tool(r = r) children();
    }
}
```

Write your own version of that when you want a different composition — a
chamfer outside and a fillet inside, say, or two different sizes. The tools are
the surface you build on; `fillet()` is just the common case.

## Choosing edges

Two things decide whether an edge is treated.

**Its sign.** A concave edge is an inside corner, where material meets material;
a convex edge is an outside corner. `fillet_tool` and `chamfer_tool` follow the
concave ones, `round_tool` and `bevel_tool` the convex ones.

**How sharply it turns.** An edge counts as a feature of the shape when it turns
by more than 1.5× the facet angle *you* are working at — 22.5° at `$fn = 24`.
Below that it is read as tessellation and left alone, which is what keeps a
cylinder's sides smooth while its rims get rounded. `min_angle = <degrees>`
overrides this when it picks the wrong edges.

## Picking particular edges: brushes

Children after the first are **selection brushes**. A brush is an ordinary
solid, and the tool acts only where a crease lies inside it.

![Selective blending](fig-selective.png)

*A block with a blind bore, near half cut away. The bore floor is filleted, the
bore mouth is rounded, and the block's own corners are left sharp — which the
sign of an edge cannot express on its own, since the mouth and the corners are
both convex.*

```openscad
module part() {
    difference() {
        cube([40, 40, 20]);
        translate([20, 20, 6]) cylinder(r = 8, h = 20, $fn = 48);
    }
}

difference() {
    union() {
        part();
        fillet_tool(r = 2) part();          // the bore floor - no brush needed,
    }                                       // it is the only concave crease
    round_tool(r = 2) {
        part();
        translate([20, 20, 14]) cylinder(r = 14, h = 8);    // brush: the mouth
    }
}
```

Two things to know about brushes:

- **A brush selects; it does not shape.** The blend is the full size you asked
  for wherever it is built, no matter how small the brush is.
- **A blend stops square.** Where the brush boundary crosses an edge, the blend
  ends in a flat cap. There is no taper back into the sharp edge.

## Curved edges

The edge a blend follows does not have to be straight, and the surfaces it
blends do not have to be flat.

![Curved creases](fig-curved.png)

*Left: a weld fillet where a branch pipe lands on a run pipe — the seam between
two cylinders, whose angle changes at every point along it. Middle: two
overlapping bosses, whose base rings cross each other at two points. Right: a
dome, a wall curved in both directions at once.*

## When something does not fit

A size is never quietly reduced. If a blend of the size you asked for cannot be
built along an edge, that edge is left alone and you are told which one:

```
WARNING: fillet_tool: radius 2 does not fit the crease at [12, -12, 2] -
         another feature 1.69 away needs the same material. That crease is
         dropped; the size is never clamped to make it fit.
```

Every other edge on the model is still blended. The two ways a size fails to fit
are that the blend would run off the end of a face it is meant to meet, or that
a second feature nearby needs the same material.

## Things to know

- **Preview shows the model unblended.** `fillet()` hands back its child
  untouched under F5, because the blends cost real work on every keystroke.
  Press F6, or pass `disable_preview = false`. The four `*_tool` modules always
  build.
- **One size per call.** Variable radius along an edge, and different radii
  meeting at a corner, are not supported.
- **Blending an already-blended model is unreliable.** A finished blend meets
  its wall tangentially, and a tessellated tangency produces slivers that read
  back as edges the shape does not have. Blend the source model, not the result.
- **`offset(chamfer = true)` is a different thing** — that is a 2D polygon
  offset style, unrelated to `chamfer_tool()`.
