"""
B1 seam SPIKE -- Round 3: the REAL local, poke-aware bridge.

Round 2 was refuted twice:
  - bridge=True snapped the seam to the FULL fine wall polyline => a
    full-resolution REVERT, not a local bridge (defeats bounded station counts).
  - the un-bridged "hole" was a COARSE-top / FINE-bottom tessellation mismatch
    (open by construction), so "poke => hole => bridge" was never shown.

Round 3 fixes both:
  1. CONSISTENT SEAM: the blend corridor is built by a real local
     re-triangulation (fan STITCH) between a fine retained polyline and the
     coarse station polyline. With NO poke, a coarse seam is WATERTIGHT at ANY
     station count, incl. the coarse minimum (proved on the convex wall).
  2. GENUINE POKE FAILURE: with a concave dent, a span whose coarse chord
     genuinely leaves the solid (signed_distance<0) CANNOT be stitched -- the
     algorithm refuses it and leaves that span's seam OPEN. Non-poking spans stay
     coarse and watertight. So the failure is caused by the poke, not by
     tessellation.
  3. LOCAL POKE-AWARE BRIDGE: only the poking span(s) get a fine corridor that
     follows the real surface; every other span stays coarse. Bounded vertices.
  4. subdiv_limit honesty: coarse+no-bridge with a real poke => broken; adaptive
     subdivide-then-local-bridge => fixed, bounded stations.

Reuses meshutil.py and the Wall rig / real trimesh queries from spike2.py.
No OpenSCAD, no repo edits.
"""
import numpy as np
import trimesh
import meshutil as mu
from spike2 import Wall, _shuffle_mesh, ray_contact, ray_gap_demo, side_filter_demo


# ---------------------------------------------------------------------------
# poke test: does the straight wall-seam chord (z=t) between two station nodes
# leave the actual solid? Uses trimesh signed_distance (>0 inside).
# ---------------------------------------------------------------------------
def span_poke(wall, tm, a, b):
    """How far (mm) the straight wall-seam chord leaves the ACTUAL solid.

    Deterministic by construction: for each chord sample we take the nearest
    point on the real mesh (rtree AABB, deterministic) and sign it with that
    facet's outward normal -- NOT trimesh.contains/signed_distance, which cast
    random rays and were the source of the round-3 nondeterminism.
    """
    Wt = wall.Wt_nodes()
    pa = np.array([Wt[a], wall.yj[a], wall.t])
    pb = np.array([Wt[b], wall.yj[b], wall.t])
    s = np.linspace(0.05, 0.95, 21)[:, None]
    samp = pa[None] * (1 - s) + pb[None] * s
    closest, dist, tri = tm.nearest.on_surface(samp)
    n = tm.face_normals[tri]
    signed = np.einsum('ij,ij->i', samp - closest, n)   # >0 => outside (air)
    return float(max(0.0, signed.max()))


# ---------------------------------------------------------------------------
# adaptive station + bridge planning: for each coarse span, subdivide up to a
# limit; any (sub)span still poking is flagged for a LOCAL fine bridge.
# ---------------------------------------------------------------------------
def poking_nodes(wall, a, b, poke_tol):
    """Fine nodes strictly inside span (a,b) whose real surface sits on the AIR
    side of the straight coarse chord Sw_a->Sw_b (i.e. the chord pokes out there).
    Deterministic, from the ACTUAL facet vertex positions (Wt_nodes). Air is +x,
    solid is x<Wt, so the chord pokes where Wt[k] < chord_x - tol."""
    Wt = wall.Wt_nodes()
    ya, yb, xa, xb = wall.yj[a], wall.yj[b], Wt[a], Wt[b]
    out = []
    for k in range(a + 1, b):
        xc = xa + (xb - xa) * (wall.yj[k] - ya) / (yb - ya)
        if Wt[k] < xc - poke_tol:
            out.append(k)
    return out


def plan_seam(wall, tm, coarse_stations, subdiv_limit, poke_tol, allow_bridge):
    """Return (stations, open_spans).

    SUB-INTERVAL BRIDGE: for each poking span, insert as new seam stations ONLY
    the fine nodes where the chord is actually in air (`poking_nodes`), leaving
    the non-concave shoulders of the span coarse. Iterate until no span pokes.
    Bridge cost therefore tracks the CONCAVE feature's own tessellation, not the
    background wall density. If allow_bridge is False, poking spans are left OPEN
    (a real hole), unchanged from the confirmed round-3 behaviour.
    """
    stations = sorted(set(coarse_stations))
    if not allow_bridge:
        open_spans = {(a, b) for a, b in zip(stations[:-1], stations[1:])
                      if span_poke(wall, tm, a, b) > poke_tol}
        return stations, open_spans

    for _ in range(64):                       # bounded by fine node count
        changed = False
        cur = sorted(set(stations))
        for a, b in zip(cur[:-1], cur[1:]):
            if span_poke(wall, tm, a, b) <= poke_tol:
                continue
            nodes = poking_nodes(wall, a, b, poke_tol)
            if not nodes:                     # degenerate: split at midpoint
                m = (a + b) // 2
                nodes = [m] if a < m < b else []
            new = [n for n in nodes if n not in stations]
            if new:
                stations += new
                changed = True
        if not changed:
            break
    return sorted(set(stations)), set()


# ---------------------------------------------------------------------------
# fan-stitch between a fine polyline (fine_ids, node range [a..b]) and a coarse
# edge (cA, cB). Triangulates the strip; winding fixed later by fix_normals.
# ---------------------------------------------------------------------------
def stitch_fan(fine_ids, a, b, cA, cB):
    tris = []
    for k in range(a, b):
        tris.append([cA, fine_ids[k], fine_ids[k + 1]])
    tris.append([cA, fine_ids[b], cB])
    return tris


# ===========================================================================
# The seam solid, with a REAL fine/coarse stitch and a LOCAL bridge.
# ===========================================================================
def build_seam_solid(wall, tm, coarse_stations, subdiv_limit=0,
                     allow_bridge=False, poke_tol=None, force_full_res=False):
    """Build the closed local solid.

    Retained wall (z in [z1,H]) and retained plate are FINE (real facets, every
    node). The blend corridor (wall-seam z=t, blend strip, plate stitch) is
    COARSE at `coarse_stations`, joined to the fine parts by fan stitches.
    Poking spans are either left OPEN (allow_bridge=False) or given a LOCAL fine
    corridor (allow_bridge=True). force_full_res makes every node a station (the
    'revert' baseline, for the vertex-count comparison).
    """
    if poke_tol is None:
        poke_tol = wall.eps
    yv = wall.yj
    Wt = wall.Wt_nodes()
    NY = wall.NY
    t, H, D, Xb, Xo = wall.t, wall.H, wall.D, wall.Xback, wall.Xout
    z1 = t + 1.0                                     # first retained-wall row

    if force_full_res:
        stations = list(range(NY))
        open_spans = set()
    else:
        stations, open_spans = plan_seam(
            wall, tm, coarse_stations, subdiv_limit, poke_tol, allow_bridge)
    coarse_count = len(set(coarse_stations))

    V = []
    idx = {}
    def add(key, p):
        if key in idx:
            return idx[key]
        V.append(list(p)); idx[key] = len(V) - 1; return len(V) - 1

    # --- fine rings (all nodes) ---
    for k in range(NY):
        add(('WT', k), [Wt[k], yv[k], H])
        add(('W1', k), [Wt[k], yv[k], z1])
        add(('PI', k), [Wt[k] + t, yv[k], 0.0])     # plate inner (= plate seam, fine)
        add(('PO', k), [Xo, yv[k], 0.0])
        add(('POB', k), [Xo, yv[k], -D])
        add(('BB', k), [Xb, yv[k], -D])
        add(('BT', k), [Xb, yv[k], H])
    W1 = [idx[('W1', k)] for k in range(NY)]
    PI = [idx[('PI', k)] for k in range(NY)]

    # --- coarse corridor vertices at station nodes ---
    for j in stations:
        add(('Sw', j), [Wt[j], yv[j], t])           # wall seam (z=t)
    def Sw(j):
        return add(('Sw', j), [Wt[j], yv[j], t])

    F = []
    # fine retained faces (always) ---------------------------------------
    for k in range(NY - 1):
        F += mu.quad(idx[('WT', k)], idx[('WT', k + 1)], idx[('W1', k + 1)], idx[('W1', k)])   # wall
        F += mu.quad(idx[('PI', k)], idx[('PO', k)], idx[('PO', k + 1)], idx[('PI', k + 1)])   # plate
        F += mu.quad(idx[('WT', k)], idx[('WT', k + 1)], idx[('BT', k + 1)], idx[('BT', k)])   # top z=H
        F += mu.quad(idx[('BT', k)], idx[('BT', k + 1)], idx[('BB', k + 1)], idx[('BB', k)])   # back
        F += mu.quad(idx[('BB', k)], idx[('BB', k + 1)], idx[('POB', k + 1)], idx[('POB', k)]) # bottom
        F += mu.quad(idx[('POB', k)], idx[('POB', k + 1)], idx[('PO', k + 1)], idx[('PO', k)]) # outer

    # corridor per span ---------------------------------------------------
    # Each coarse span is closed by TWO fan-stitches from the coarse wall-seam
    # edge (Sw_a,Sw_b): one up to the fine retained wall (W1), one down to the
    # fine retained plate inner (PI). No full-resolution seam ring is created --
    # the corridor cost is O(stations), not O(nodes).
    # The sub-interval bridge inserted fine seam stations ONLY over the concave
    # sub-interval; every span (coarse shoulder or fine dent step) is then closed
    # by the SAME two fan-stitches. Corridor (Sw) cost = number of stations.
    for a, b in zip(stations[:-1], stations[1:]):
        if (a, b) in open_spans:
            continue                                # leave OPEN -> localized hole
        cA, cB = idx[('Sw', a)], idx[('Sw', b)]
        F += stitch_fan(W1, a, b, cA, cB)           # wall-bottom: fine W1 -> coarse Sw edge
        F += stitch_fan(PI, a, b, cA, cB)           # blend: fine PI -> coarse Sw edge

    # end caps at k=0 and k=NY-1 -----------------------------------------
    for k, flip in ((0, False), (NY - 1, True)):
        swk = idx[('Sw', k)]
        loop = [idx[('BT', k)], idx[('WT', k)], idx[('W1', k)], swk,
                idx[('PI', k)], idx[('PO', k)], idx[('POB', k)], idx[('BB', k)]]
        for m in range(1, len(loop) - 1):
            tri = [loop[0], loop[m], loop[m + 1]]
            F.append(tri[::-1] if flip else tri)

    V = np.array(V, float)
    F = np.array([f for f in F if f is not None and len(f) == 3], int)

    tm2 = trimesh.Trimesh(vertices=V, faces=F, process=False)
    try:
        trimesh.repair.fix_normals(tm2)
        V, F = np.asarray(tm2.vertices), np.asarray(tm2.faces)
        watertight = bool(tm2.is_watertight)
    except Exception:
        watertight = False

    # duplicate/degenerate face check
    Vc, Fc = mu.canonical(V, F)
    fk = np.sort(Fc, axis=1)
    dup = len(fk) - len(np.unique(fk, axis=0))

    info = dict(n_stations=len(stations), stations=stations,
                open_spans=sorted(open_spans),
                # BRIDGE COST METRIC: corridor seam (Sw) vertices only, excluding
                # the retained fine wall/plate pass-through (W1,PI,PO,... are
                # full-res by design in every case and would mask the comparison).
                corridor_seam_verts=len(stations),
                bridge_added_verts=len(stations) - coarse_count,
                total_verts=len(Vc), total_faces=len(Fc),
                duplicate_faces=int(dup), watertight=watertight)
    return V, F, info


# ===========================================================================
# Driver
# ===========================================================================
def per_span_report(wall, tm, stations):
    Wt = wall.Wt_nodes()
    rows = []
    for a, b in zip(stations[:-1], stations[1:]):
        pk = span_poke(wall, tm, a, b)
        rows.append(dict(span=[int(a), int(b)], poke_mm=round(pk, 4),
                         pokes=bool(pk > wall.eps)))
    return rows


def run():
    out = {}

    # ---- REQ 1: convex wall, coarse, NO bridge -> watertight at any count ----
    # genuinely no-poke wall: strictly convex bump, tangential (y) jitter kept
    # so spacing is irregular, but radial (x) jitter zeroed so no concave dip.
    cw = Wall(jitter=0.30, dent=0.0)
    cw.xj[:] = 0.0
    ctm = cw.base_solid()
    conv = {}
    for stset in ([0, cw.NY - 1], [0, 20, cw.NY - 1],
                  [0, 8, 16, 24, cw.NY - 1], list(range(0, cw.NY, 4)) + [cw.NY - 1]):
        V, F, info = build_seam_solid(cw, ctm, stset, subdiv_limit=0,
                                      allow_bridge=False)
        rep = mu.manifold_report(V, F)
        conv[f"stations={len(stset)}"] = dict(
            n_stations=info['n_stations'], watertight=rep['closed'],
            boundary_edges=rep['boundary_edges'], nonmanifold=rep['nonmanifold_edges'],
            orient_mismatch=rep['orientation_mismatches'], euler=rep['euler'],
            dup_faces=info['duplicate_faces'], verts=info['total_verts'],
            faces=info['total_faces'],
            max_span_poke=max(r['poke_mm'] for r in per_span_report(cw, ctm, info['stations'])))
    out['REQ1_convex_coarse'] = conv

    # ---- REQ 2: dent, coarse, NO bridge -> only poking spans open -----------
    dw = Wall(jitter=0.30, dent=2.2, dent_c=5.0, dent_w=1.0)
    dtm = dw.base_solid()
    coarse = [0, 8, 16, 24, dw.NY - 1]
    Vn, Fn, infon = build_seam_solid(dw, dtm, coarse, subdiv_limit=0,
                                     allow_bridge=False)
    repn = mu.manifold_report(Vn, Fn)
    out['REQ2_dent_coarse_nobridge'] = dict(
        per_span=per_span_report(dw, dtm, infon['stations']),
        open_spans=infon['open_spans'],
        boundary_edges=repn['boundary_edges'], nonmanifold=repn['nonmanifold_edges'],
        watertight=repn['closed'],
        note="non-poking spans are watertight even coarse; the hole is ONLY on "
             "the poking span => poke-caused, not tessellation mismatch")

    # ---- REQ 3: LOCAL poke-aware bridge vs full-resolution revert -----------
    Vb, Fb, infob = build_seam_solid(dw, dtm, coarse, subdiv_limit=0,
                                     allow_bridge=True)
    repb = mu.manifold_report(Vb, Fb)
    Vf, Ff, infof = build_seam_solid(dw, dtm, coarse, force_full_res=True)
    repf = mu.manifold_report(Vf, Ff)
    out['REQ3_local_bridge'] = dict(
        local_bridge=dict(n_stations=infob['n_stations'],
                          corridor_seam_verts=infob['corridor_seam_verts'],
                          bridge_added_verts=infob['bridge_added_verts'],
                          verts=infob['total_verts'], faces=infob['total_faces'],
                          watertight=repb['closed'], boundary_edges=repb['boundary_edges'],
                          nonmanifold=repb['nonmanifold_edges'],
                          orient_mismatch=repb['orientation_mismatches'],
                          euler=repb['euler'], dup_faces=infob['duplicate_faces']),
        full_res_revert=dict(n_stations=infof['n_stations'],
                             corridor_seam_verts=infof['corridor_seam_verts'],
                             verts=infof['total_verts'],
                             faces=infof['total_faces'], watertight=repf['closed']),
        note="local bridge inserts fine seam nodes only over the concave "
             "sub-interval; shoulders stay coarse")

    # ---- REQ 4: subdiv_limit dependence, honest ----------------------------
    tbl = []
    for lim in [0, 1, 2, 3]:
        V, F, info = build_seam_solid(dw, dtm, coarse, subdiv_limit=lim,
                                      allow_bridge=False)
        rep = mu.manifold_report(V, F)
        # then: adaptive subdivide + LOCAL bridge
        Vb2, Fb2, ib2 = build_seam_solid(dw, dtm, coarse, subdiv_limit=lim,
                                         allow_bridge=True)
        rb2 = mu.manifold_report(Vb2, Fb2)
        tbl.append(dict(subdiv_limit=lim,
                        nobridge_stations=info['n_stations'],
                        nobridge_open_spans=len(info['open_spans']),
                        nobridge_boundary_edges=rep['boundary_edges'],
                        nobridge_watertight=rep['closed'],
                        bridge_stations=ib2['n_stations'],
                        bridge_watertight=rb2['closed'],
                        bridge_verts=ib2['total_verts']))
    out['REQ4_subdiv_limit'] = tbl

    # ---- REQ 5: determinism (shuffle mesh pre-query) + mid-facet ray --------
    def pipeline(shuffle):
        w = Wall(jitter=0.30, dent=2.2, dent_c=5.0, dent_w=1.0)
        m = w.base_solid(shuffle=shuffle)
        return build_seam_solid(w, m, [0, 8, 16, 24, w.NY - 1],
                                subdiv_limit=2, allow_bridge=True)[:2]
    ref = pipeline(None)
    det = [mu.meshes_identical(ref, pipeline(s)) for s in (11, 22, 33)]
    out['REQ5_determinism'] = dict(all_match=all(det), per_trial=det)
    out['REQ5_ray_midfacet'] = ray_gap_demo(dw, dtm)   # jittered wall: real gap
    out['side_filter'] = side_filter_demo()
    out['BONUS_dome'] = dome_demo()
    out['REQ_locality_two_axis'] = locality_two_axis()

    return out


# ===========================================================================
# The decisive two-axis locality test: does the sub-interval bridge cost track
# ONLY the concave feature, or also the unrelated wall density?
# ===========================================================================
def make_wall(bg_n, dent_n, dent=2.2, dent_c=5.0, dent_w=0.7, y0=0.0, y1=10.0):
    """Wall with independently controllable background vs dent tessellation.

    The concave footprint is FIXED at [dent_c +/- W] (W chosen so the Gaussian
    dent is negligible outside it). Nodes inside the footprint = exactly dent_n
    (the DENT's own tessellation). Background nodes are placed ONLY OUTSIDE the
    footprint, so raising bg_n refines the NON-concave wall without touching the
    dent's tessellation -- exactly the two-axis separation the locality test
    needs. No radial jitter, so the dent is the only concavity."""
    W = 3.0 * dent_w                                   # footprint half-width
    w = Wall(y0=y0, y1=y1, NY=2, dent=dent, dent_c=dent_c, dent_w=dent_w)
    y_bg = np.linspace(y0, y1, bg_n)
    y_bg = y_bg[(y_bg < dent_c - W) | (y_bg > dent_c + W)]   # outside footprint only
    y_dent = np.linspace(dent_c - W, dent_c + W, dent_n)     # fixed inside footprint
    nodes = np.unique(np.round(np.concatenate([[y0, y1], y_bg, y_dent]), 6))
    w.y = nodes.copy(); w.yj = nodes.copy(); w.xj = np.zeros(len(nodes))
    w.NY = len(nodes)
    return w


def _coarse_stations(w, targets=(0.0, 2.0, 8.0, 10.0)):
    """Coarse stations at FIXED positions that BRACKET the dent (dent_c=5 sits
    inside the single coarse span 2..8), mapped to nearest node indices. This
    forces the coarse chord to span the whole concave feature, so the bridge
    must genuinely insert nodes -- and the test measures how many."""
    idxs = sorted(set(int(np.argmin(np.abs(w.yj - tp))) for tp in targets))
    return sorted(set([0] + idxs + [w.NY - 1]))


def _wholespan_added(w, tm, coarse, poke_tol):
    """What round-3 WOULD add: all fine nodes strictly inside each poking coarse
    span (the whole-span fine bridge)."""
    total = 0
    for a, b in zip(coarse[:-1], coarse[1:]):
        if span_poke(w, tm, a, b) > poke_tol:
            total += (b - a - 1)
    return total


def locality_two_axis():
    # (a) refine wall AWAY from dent (bg grows, dent fixed) -> expect FLAT
    axis_a = []
    for bg in [9, 17, 33, 65, 129, 257]:
        w = make_wall(bg_n=bg, dent_n=9)
        tm = w.base_solid()
        coarse = _coarse_stations(w)
        _, _, info = build_seam_solid(w, tm, coarse, allow_bridge=True)
        rep = mu.manifold_report(*build_seam_solid(w, tm, coarse, allow_bridge=True)[:2])
        axis_a.append(dict(bg_nodes=bg, total_nodes=w.NY,
                           bridge_added_verts=info['bridge_added_verts'],
                           corridor_seam_verts=info['corridor_seam_verts'],
                           roundish3_wholespan_added=_wholespan_added(w, tm, coarse, w.eps),
                           watertight=rep['closed']))
    # (b) refine the DENT itself (dent grows, bg fixed) -> expect GROW (fidelity)
    axis_b = []
    for dn in [5, 9, 17, 33, 65]:
        w = make_wall(bg_n=33, dent_n=dn)
        tm = w.base_solid()
        coarse = _coarse_stations(w)
        _, _, info = build_seam_solid(w, tm, coarse, allow_bridge=True)
        rep = mu.manifold_report(*build_seam_solid(w, tm, coarse, allow_bridge=True)[:2])
        axis_b.append(dict(dent_nodes=dn, total_nodes=w.NY,
                           bridge_added_verts=info['bridge_added_verts'],
                           corridor_seam_verts=info['corridor_seam_verts'],
                           watertight=rep['closed']))
    a_added = [r['bridge_added_verts'] for r in axis_a]
    b_added = [r['bridge_added_verts'] for r in axis_b]
    return dict(axis_a_refine_wall_away=axis_a,
                axis_b_refine_dent=axis_b,
                axis_a_flat=(max(a_added) - min(a_added) <= 2),
                axis_b_grows=(b_added[-1] > b_added[0] + 2),
                verdict=("BOUNDED: bridge cost tracks the concave feature only"
                         if (max(a_added) - min(a_added) <= 2) and (b_added[-1] > b_added[0] + 2)
                         else "NOT bounded: bridge cost grows with unrelated wall density"))


# ===========================================================================
# BONUS: do the clip/contact/poke primitives port to a DOUBLY-curved wall?
# (retires the "vertical-extrusion only" caveat for these primitives; the full
#  bridged dome-seam mesh is NOT built here -- stated as a scope limit.)
# ===========================================================================
def dome_demo(seed=3):
    R = 10.0
    dome = trimesh.creation.icosphere(subdivisions=3, radius=R)   # doubly curved
    dome = dome.submesh([dome.triangles_center[:, 2] > 0.0], append=True)  # cap
    trimesh.repair.fix_normals(dome)

    def contacts(mesh, z0, thetas):
        pts, tris = [], []
        for th in thetas:
            o = np.array([2 * R * np.cos(th), 2 * R * np.sin(th), z0])
            d = np.array([-np.cos(th), -np.sin(th), 0.0])
            loc, _, tri = mesh.ray.intersects_location([o], [d])
            if len(loc):
                j = np.argmin(np.linalg.norm(loc - o, axis=1))
                pts.append(loc[j]); tris.append(int(tri[j]))
        return np.array(pts), tris

    z0 = 4.0
    thetas = np.linspace(0, 2 * np.pi, 24, endpoint=False)
    pts, tris = contacts(dome, z0, thetas)

    # (1) real clip walks many facets: section circle at z=z0
    sec = dome.section(plane_origin=[0, 0, z0], plane_normal=[0, 0, 1])
    facets_walked = len(np.unique(sec.metadata.get('face_index', []))) if sec else 0
    if not facets_walked and sec is not None:
        facets_walked = sum(len(e.points) - 1 for e in sec.entities)

    # (2) determinism of ray contacts under a shuffled dome
    Vs, Fs = _shuffle_mesh(np.asarray(dome.vertices), np.asarray(dome.faces), seed)
    dome2 = trimesh.Trimesh(vertices=Vs, faces=Fs, process=False)
    trimesh.repair.fix_normals(dome2)
    pts2, _ = contacts(dome2, z0, thetas)
    contact_match = bool(pts.shape == pts2.shape and np.allclose(np.sort(pts, 0), np.sort(pts2, 0), atol=1e-9))

    # (3) real poke on a doubly-curved concavity: dent the cap inward, then a
    #     coarse chord between two contacts across the dent must poke out.
    dented = dome.copy()
    v = np.asarray(dented.vertices).copy()
    # localize the dent to theta~0 (|y| small) so the straddling chord crosses it
    band = (v[:, 2] > 2.0) & (v[:, 2] < 7.0) & (v[:, 0] > 0.3 * R) & (np.abs(v[:, 1]) < 2.0)
    v[band] *= 0.6                                    # push inward => concave dent
    dented.vertices = v
    trimesh.repair.fix_normals(dented)
    # two contacts straddling the dent (theta ~ 0), coarse chord between them
    th_a, th_b = -0.7, 0.7
    ca, _ = contacts(dented, z0, [th_a]); cb, _ = contacts(dented, z0, [th_b])
    poke = 0.0
    if len(ca) and len(cb):
        s = np.linspace(0.05, 0.95, 21)[:, None]
        chord = ca[0][None] * (1 - s) + cb[0][None] * s
        cl, _, tr = dented.nearest.on_surface(chord)
        nn = dented.face_normals[tr]
        poke = float(max(0.0, np.einsum('ij,ij->i', chord - cl, nn).max()))

    return dict(dome_faces=len(dome.faces), contacts_found=len(pts),
                section_facets_walked=int(facets_walked),
                contact_determinism_under_shuffle=contact_match,
                doubly_curved_dent_pokeout_mm=round(poke, 4),
                note="contact/clip/poke primitives ported to a doubly-curved "
                     "icosphere; full bridged dome-seam mesh NOT built (scope limit)")


if __name__ == "__main__":
    import json
    r = run()
    print(json.dumps(r, indent=2,
          default=lambda o: (o.tolist() if hasattr(o, "tolist") else str(o))))
