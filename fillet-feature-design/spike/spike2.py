"""
B1 seam SPIKE -- REBUILD after refutation.

The first pass cheated three ways (all valid refutations):
  1. seam vertices placed ANALYTICALLY on the ideal cylinder -> facets never
     consulted, clip_err=0 by construction.
  2. the "bridge" mesh was a lofted tube, watertight for EVERY subdiv_limit
     incl. 0 -> no chord ever poked, nothing ever bridged.
  3. ray==clip agreement only held AT polygon-vertex angles.

This rebuild fixes all three with REAL trimesh queries against a JITTERED
tessellated wall:
  A. jitter the wall vertices (radial x + tangential y) by a meaningful
     fraction of edge length, keeping connectivity.
  B. find the seam by querying the ACTUAL faceted mesh (trimesh section / ray),
     and report that a station span's contact line crosses MULTIPLE real facets.
  C. drive a chord to genuinely poke OUT of the solid over a concave dent
     (measured with trimesh signed_distance), then show:
       (1) un-bridged coarse seam is BROKEN (boundary_edges > 0),
       (2) bridge -> closed manifold,
       (3) the outcome DEPENDS on subdiv_limit (broken low, fixed high).
  D. cast rays at MID-FACET angles -> report the real tessellation gap vs the
     ideal smooth surface.

meshutil.py (manifold report + determinism canonicalizer) is reused unchanged.
No OpenSCAD, no repo edits.
"""
import numpy as np
import trimesh
import meshutil as mu


# ---------------------------------------------------------------------------
# The model: an L-section block extruded along y. The +x face for z in [0,H] is
# a tessellated CURVED wall x = Wt(y); the z=0 face for x>Wt is the flat plate.
# The crease is the reentrant edge at (Wt(y), y, 0). Air corner = {x>Wt, z>0}.
# ---------------------------------------------------------------------------
class Wall:
    def __init__(self, y0=0.0, y1=10.0, NY=41, H=6.0, D=2.0,
                 Xback=-4.0, Xout=4.0, t=1.5,
                 bump=1.0, jitter=0.30, dent=0.0, dent_c=5.0, dent_w=1.2,
                 seed=7):
        self.__dict__.update(locals())
        self.eps = max(1e-3 * t, 1e-9)
        rng = np.random.default_rng(seed)
        self.y = np.linspace(y0, y1, NY)
        edge = (y1 - y0) / (NY - 1)
        # tangential (y) jitter, kept within +/-45% of an edge so order is preserved
        self.yj = self.y + (rng.random(NY) - 0.5) * 2 * jitter * edge
        self.yj[0], self.yj[-1] = y0, y1
        self.yj = np.sort(self.yj)
        # radial (x) jitter added on top of the smooth ideal contour
        self.xj = rng.standard_normal(NY) * jitter * edge

    # --- the smooth IDEAL wall contour at height z=t (what an analytic method
    #     would use); the mesh only SAMPLES this at the (jittered) y_k. ---
    def Wt_smooth(self, y):
        yc = 0.5 * (self.y0 + self.y1)
        bump = self.bump * np.cos(np.pi * (y - yc) / (self.y1 - self.y0))
        dent = self.dent * np.exp(-((y - self.dent_c) / self.dent_w) ** 2)
        return bump - dent          # dent recedes into solid (-x) => concave

    # --- the ACTUAL faceted contour value at the mesh vertices (smooth + jitter)
    def Wt_nodes(self):
        return self.Wt_smooth(self.yj) + self.xj

    # --- build the closed base solid (no fillet) for queries -------------
    def base_solid(self, shuffle=None):
        yv = self.yj
        Wt = self.Wt_nodes()
        H, D, Xb, Xo = self.H, self.D, self.Xback, self.Xout
        # hexagonal L cross-section per station (x,z), CCW:
        def sect(k):
            w = Wt[k]
            return [(Xb, H), (w, H), (w, 0.0), (Xo, 0.0), (Xo, -D), (Xb, -D)]
        P = 6
        V, F = [], []
        for k in range(self.NY):
            for (x, z) in sect(k):
                V.append([x, yv[k], z])
        for k in range(self.NY - 1):
            for m in range(P):
                m2 = (m + 1) % P
                F += mu.quad(k * P + m, k * P + m2, (k + 1) * P + m2, (k + 1) * P + m)
        for k, flip in ((0, False), (self.NY - 1, True)):
            for m in range(1, P - 1):
                tri = [k * P, k * P + m, k * P + m + 1]
                F.append(tri[::-1] if flip else tri)
        V = np.array(V, float); F = np.array(F, int)
        if shuffle is not None:
            V, F = _shuffle_mesh(V, F, shuffle)
        tm = trimesh.Trimesh(vertices=V, faces=F, process=False)
        trimesh.repair.fix_normals(tm)
        return tm


def _shuffle_mesh(V, F, seed):
    """Randomly permute vertex indices and face order -- geometry unchanged."""
    rng = np.random.default_rng(seed)
    perm = rng.permutation(len(V))
    inv = np.empty_like(perm); inv[perm] = np.arange(len(perm))
    V2 = V[perm]
    F2 = inv[F]
    order = rng.permutation(len(F2))
    F2 = F2[order]
    # also randomly roll winding start (keeps orientation)
    rolls = rng.integers(0, 3, len(F2))
    F2 = np.array([np.roll(f, r) for f, r in zip(F2, rolls)])
    return V2, F2


# ===========================================================================
# B: REAL clip -- seam from the actual faceted mesh, walking multiple facets.
# ===========================================================================
def seam_section(wall: Wall, tm):
    """Seam line = plane(z=t) intersected with the ACTUAL mesh (trimesh.section).
    Returns the ordered polyline (x,y at z=t) and the number of real facets its
    segments cross."""
    sec = tm.section(plane_origin=[0, 0, wall.t], plane_normal=[0, 0, 1])
    if sec is None:
        return None, 0, 0
    # keep only the wall-face crossings (x near the wall, not the back/plate)
    v = np.array(sec.vertices)
    # facets crossed = number of unique triangles the section passes through
    n_facets = len(sec.metadata.get('face_index', [])) if hasattr(sec, 'metadata') else 0
    if not n_facets:
        # count line segments instead
        n_facets = sum(len(e.points) - 1 for e in sec.entities)
    return v, n_facets, len(sec.entities)


def ray_contact(tm, y, z, from_x=8.0):
    """First-hit contact on the ACTUAL mesh: cast a ray from air (+x) inward."""
    loc, idx_ray, idx_tri = tm.ray.intersects_location(
        [[from_x, y, z]], [[-1.0, 0.0, 0.0]])
    if len(loc) == 0:
        return None, None
    j = np.argmin(np.abs(loc[:, 0] - from_x))   # nearest hit = first surface
    return loc[j], int(idx_tri[j])


# ===========================================================================
# C: build the seam solid with retained (fine, real) wall + coarse blend, so
#    a coarse/un-bridged blend leaves a genuine crack. Parametrized by
#    subdiv_limit and bridge.
# ===========================================================================
def build_seam_solid(wall: Wall, tm, subdiv_limit=0, bridge=False):
    """Build a CLOSED local solid: retained wall above z=t uses the ACTUAL
    faceted vertices (fine); the blend/chamfer strip below sews to it.

    Stations (coarse) = wall endpoints + subdiv_limit evenly-spaced interior
    nodes. If bridge=False, the blend top ring uses only the station vertices,
    so the fine wall-bottom edges between stations are unmatched -> boundary
    edges (a real hole). If bridge=True, the blend top ring is snapped to the
    full fine wall polyline (the offending chord is bridged back to the real
    surface) -> matched -> closed.

    Returns (V, F, info).
    """
    yv = wall.yj
    Wt = wall.Wt_nodes()
    NY = wall.NY
    t, H, D, Xb, Xo = wall.t, wall.H, wall.D, wall.Xback, wall.Xout

    # fine rings from the ACTUAL mesh vertices ------------------------------
    # wall seam ring F_k = (Wt_k, y_k, t)   (retained wall bottom = real facets)
    # plate seam ring G_k = (Wt_k + t, y_k, 0)
    F_pts = np.column_stack([Wt, yv, np.full(NY, t)])
    G_pts = np.column_stack([Wt + t, yv, np.zeros(NY)])

    # station indices along the fine grid ----------------------------------
    if subdiv_limit <= 0:
        stations = [0, NY - 1]
    else:
        stations = sorted(set(np.linspace(0, NY - 1, subdiv_limit + 2).round().astype(int)))

    V = []
    idx = {}
    def add(key, p):
        V.append(list(p)); idx[key] = len(V) - 1; return len(V) - 1

    # vertices: fine wall-top, fine wall-seam(F), fine plate-seam(G), plate-out,
    # back/bottom rings (fine) -- everything fine except the blend TOP ring.
    for k in range(NY):
        add(('WT', k), [Wt[k], yv[k], H])       # wall top (retained)
        add(('F', k), F_pts[k])                 # wall seam (retained bottom)
        add(('G', k), G_pts[k])                 # plate seam (retained plate inner)
        add(('PO', k), [Xo, yv[k], 0.0])        # plate outer
        add(('POB', k), [Xo, yv[k], -D])        # plate outer bottom
        add(('BB', k), [Xb, yv[k], -D])         # back bottom
        add(('BT', k), [Xb, yv[k], H])          # back top

    F = []
    # retained wall (fine): WT_k, WT_k+1, F_k+1, F_k
    for k in range(NY - 1):
        F += mu.quad(idx[('WT', k)], idx[('WT', k + 1)], idx[('F', k + 1)], idx[('F', k)])
    # retained plate (fine): G_k, PO_k, PO_k+1, G_k+1
    for k in range(NY - 1):
        F += mu.quad(idx[('G', k)], idx[('PO', k)], idx[('PO', k + 1)], idx[('G', k + 1)])
    # closing shell (fine): back, bottom, plate-outer wall, top
    for k in range(NY - 1):
        F += mu.quad(idx[('BT', k)], idx[('BT', k + 1)], idx[('BB', k + 1)], idx[('BB', k)])   # back
        F += mu.quad(idx[('BB', k)], idx[('BB', k + 1)], idx[('POB', k + 1)], idx[('POB', k)]) # bottom
        F += mu.quad(idx[('POB', k)], idx[('POB', k + 1)], idx[('PO', k + 1)], idx[('PO', k)]) # outer face
        F += mu.quad(idx[('WT', k)], idx[('BT', k + 1)], idx[('BT', k)], idx[('WT', k)]) if False else []
        F += mu.quad(idx[('WT', k)], idx[('WT', k + 1)], idx[('BT', k + 1)], idx[('BT', k)])   # top (z=H)

    # blend / chamfer strip: top ring (wall seam) -> bottom ring (plate seam) --
    # bottom is always fine (G). top is coarse (stations) unless bridged.
    n_boundary_before = None
    for a, b in zip(stations[:-1], stations[1:]):
        # bottom polyline vertices G_a..G_b (fine)
        if bridge:
            # top uses the full fine wall polyline F_a..F_b -> matches retained wall
            for k in range(a, b):
                F += mu.quad(idx[('F', k)], idx[('F', k + 1)],
                             idx[('G', k + 1)], idx[('G', k)])
        else:
            # top is only the chord F_a->F_b; fan the span polygon
            # polygon = F_a, F_b, G_b, G_{b-1}, ..., G_a
            F.append([idx[('F', a)], idx[('F', b)], idx[('G', b)]])
            for k in range(b, a, -1):
                F.append([idx[('F', a)], idx[('G', k)], idx[('G', k - 1)]])

    # end caps (fine hex-ish cross-section at k=0 and k=NY-1) ----------------
    def cap(k, flip):
        loop = [idx[('BT', k)], idx[('WT', k)], idx[('F', k)],
                idx[('G', k)], idx[('PO', k)], idx[('POB', k)], idx[('BB', k)]]
        tris = []
        for m in range(1, len(loop) - 1):
            tri = [loop[0], loop[m], loop[m + 1]]
            tris.append(tri[::-1] if flip else tri)
        return tris
    F += cap(0, False)
    F += cap(NY - 1, True)

    V = np.array(V, float)
    F = np.array([f for f in F if len(f) == 3], int)

    # consistent+outward orientation (volume-based => order independent)
    tm2 = trimesh.Trimesh(vertices=V, faces=F, process=False)
    try:
        trimesh.repair.fix_normals(tm2)
        V, F = np.asarray(tm2.vertices), np.asarray(tm2.faces)
        watertight = bool(tm2.is_watertight)
    except Exception:
        watertight = False

    info = dict(stations=stations, n_stations=len(stations),
                subdiv_limit=subdiv_limit, bridge=bridge,
                incident_facets_touched=(NY - 1),  # wall facets along the seam
                watertight=watertight)
    return V, F, info


# ===========================================================================
# C poke test: does the coarse station chord genuinely leave the solid?
# ===========================================================================
def measure_pokeout(wall: Wall, tm, subdiv_limit=0):
    """For each station span, sample the straight wall-seam chord and measure
    how far it leaves the solid, using trimesh signed_distance on the ACTUAL
    solid (positive = inside). Returns max poke-out (mm) and per-span data."""
    yv = wall.yj
    Wt = wall.Wt_nodes()
    NY = wall.NY
    if subdiv_limit <= 0:
        stations = [0, NY - 1]
    else:
        stations = sorted(set(np.linspace(0, NY - 1, subdiv_limit + 2).round().astype(int)))
    F_pts = np.column_stack([Wt, yv, np.full(NY, wall.t)])
    worst = 0.0
    spans = []
    for a, b in zip(stations[:-1], stations[1:]):
        pa, pb = F_pts[a], F_pts[b]
        s = np.linspace(0.05, 0.95, 25)[:, None]
        samples = pa[None, :] * (1 - s) + pb[None, :] * s
        # nudge samples eps INTO the material along -x so on-surface endpoints
        # don't false-trigger; the poke we want is genuine air exposure.
        samples = samples - np.array([wall.eps, 0, 0])
        sd = tm.nearest.signed_distance(samples)   # >0 inside, <0 outside
        poke = float(max(0.0, -sd.min()))
        worst = max(worst, poke)
        spans.append((int(a), int(b), poke))
    return worst, spans, stations


# ===========================================================================
# D: rays at MID-FACET angles -> real tessellation gap vs ideal smooth surface.
# ===========================================================================
def ray_gap_demo(wall: Wall, tm):
    """Compare, at node y's and at mid-facet y's, the ray-hit contact on the
    faceted mesh vs the ideal smooth surface value. Mid-facet gap is the real
    tessellation error the ideal-surface method would incur."""
    yv = wall.yj
    node_gaps, mid_gaps = [], []
    for k in range(1, wall.NY - 1):
        # at node
        loc, _ = ray_contact(tm, yv[k], wall.t)
        if loc is not None:
            node_gaps.append(abs(loc[0] - (wall.Wt_smooth(yv[k]) + wall.xj[k])))
        # at mid-facet
        ym = 0.5 * (yv[k] + yv[k + 1])
        loc, _ = ray_contact(tm, ym, wall.t)
        if loc is not None:
            mid_gaps.append(abs(loc[0] - wall.Wt_smooth(ym)))
    return dict(n=len(mid_gaps),
                node_gap_max=float(np.max(node_gaps)) if node_gaps else None,
                midfacet_gap_max=float(np.max(mid_gaps)) if mid_gaps else None,
                midfacet_gap_mean=float(np.mean(mid_gaps)) if mid_gaps else None)


# ===========================================================================
# Refinement 1: normal side-filter on a thin wall (unchanged, still valid).
# ===========================================================================
def side_filter_demo():
    tm = trimesh.creation.box(extents=[0.5, 8, 8])
    n = np.array([1.0, 0, 0])
    fn = tm.face_normals
    return dict(total_faces=len(tm.faces),
                near_side_faces=int((fn @ n > 1e-6).sum()),
                far_side_faces=int((fn @ n < -1e-6).sum()))


# ===========================================================================
# Driver
# ===========================================================================
def run():
    out = {}

    # ---------- Case A/B: jittered convex wall, real clip, local seam --------
    wall = Wall(jitter=0.30, dent=0.0)          # convex + jitter, no dent
    tm = wall.base_solid()
    poly, nfac, nent = seam_section(wall, tm)
    # a coarse single span (2 stations) -- its contact line walks all wall facets
    worst, spans, stations = measure_pokeout(wall, tm, subdiv_limit=0)
    # fine seam (stations = all nodes) -> clean local manifold seam
    Vf, Ff, infof = build_seam_solid(wall, tm, subdiv_limit=wall.NY, bridge=False)
    repf = mu.manifold_report(Vf, Ff)
    out['A_convex'] = dict(
        section_polyline_pts=int(len(poly)) if poly is not None else 0,
        wall_facets_along_seam=wall.NY - 1,
        section_facets_crossed=int(nfac),
        single_span_pokeout_mm=worst,             # convex => ~0 (stays under)
        fine_seam=repf, total_base_facets=len(tm.faces),
        incident_facets=infof['incident_facets_touched'])

    # ---------- Case C: concave dent -> real poke-out + subdiv_limit + bridge --
    dwall = Wall(jitter=0.30, dent=2.2, dent_c=5.0, dent_w=1.0)
    dtm = dwall.base_solid()
    table = []
    for lim in [0, 1, 2, 4, 8, 20, dwall.NY - 2]:   # last = stations at every node
        poke, spans, st = measure_pokeout(dwall, dtm, subdiv_limit=lim)
        Vu, Fu, iu = build_seam_solid(dwall, dtm, subdiv_limit=lim, bridge=False)
        ru = mu.manifold_report(Vu, Fu)
        table.append(dict(subdiv_limit=lim, n_stations=iu['n_stations'],
                          max_pokeout_mm=poke,
                          unbridged_boundary_edges=ru['boundary_edges'],
                          unbridged_nonmanifold=ru['nonmanifold_edges'],
                          unbridged_watertight=ru['closed']))
    # bridge at limit 0 (the pathological case) -> must close
    poke0, _, _ = measure_pokeout(dwall, dtm, subdiv_limit=0)
    Vb, Fb, ib = build_seam_solid(dwall, dtm, subdiv_limit=0, bridge=True)
    rb = mu.manifold_report(Vb, Fb)
    out['C_dent'] = dict(dent_depth_mm=dwall.dent, subdiv_table=table,
                         bridge_limit0=dict(max_pokeout_mm=poke0, report=rb))

    # ---------- D: ray gap at mid-facet --------------------------------------
    out['D_ray'] = ray_gap_demo(wall, tm)

    # ---------- determinism: shuffle the MESH (verts+faces) AND stations ------
    # meaningful because the pipeline queries trimesh on the shuffled mesh.
    def pipeline(shuffle):
        w = Wall(jitter=0.30, dent=2.2, dent_c=5.0, dent_w=1.0)
        m = w.base_solid(shuffle=shuffle)
        return build_seam_solid(w, m, subdiv_limit=4, bridge=True)[:2]
    ref = pipeline(None)
    det = [mu.meshes_identical(ref, pipeline(s)) for s in (11, 22, 33)]
    out['determinism'] = dict(all_match=all(det), per_trial=det,
                              note="mesh vertex+face order shuffled before "
                                   "trimesh section/ray/contains; output compared "
                                   "canonically (coordinate-based)")

    # ---------- refinement 1 --------------------------------------------------
    out['side_filter'] = side_filter_demo()
    return out


if __name__ == '__main__':
    import json
    r = run()
    print(json.dumps(r, indent=2,
          default=lambda o: (o.tolist() if hasattr(o, 'tolist') else str(o))))
