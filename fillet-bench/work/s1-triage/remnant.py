#!/usr/bin/env python3
"""Measure the SCALE of what makes a mesh invalid, in millimetres.

S1 asks one question of every not-valid sweep cell: how big is the remnant,
against how big the solid is. `mesh.py` says a cell is invalid and by which
statistic; this says how many millimetres across the offending geometry is.

Three remnant shapes appear on this bench and each is measured differently:

  detached part   a face-component that is not the solid. Its bounding box is
                  the answer, and mesh.py already reports it; repeated here so
                  every cell is measured by one instrument.
  pinch           a vertex whose faces form more than one fan. The remnant is
                  the smaller fan's connected face patch, and its bounding box
                  is the answer. A vertex-attached patch is usually also its own
                  face-component, so the two readings agree -- which is the
                  check, not a redundancy.
  non-manifold    an edge carried by more than two faces. There is no single
                  patch to measure, so three numbers are given: the length of
                  the non-manifold edges, the bounding box of the whole
                  non-manifold locus, and the smallest triangle touching it.
                  A zero-thickness membrane -- an exact duplicate triangle pair
                  of opposite orientation -- is named as such and NOT given a
                  thickness, because it has none.

KNOWN ANSWERS this must reproduce before it is believed (--check runs them):
  tee_small at defaults   a detached 6-triangle part, 0.35 x 0.10 x 0.40 mm
  cross r=0.9             a detached 4-triangle shard, 3.45e-7 mm^3
  tee r=0.9               one pinch at (1.913417162, -4.619397663, 8.086582838)
  refused_neighbour r=0.9 a sliver wedge on 4 faces, 0.34 um across

Weld tolerance is 1e-6 unless --tol says otherwise; a reading without one is
not a number (TRAPS 6, 15).
"""

import argparse
import collections
import json
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import mesh as meshpy      # noqa: E402  (fillet-bench/mesh.py)


def weld(verts, faces, tol):
    inv = 1.0 / tol
    key, pos, remap = {}, [], []
    for x, y, z in verts:
        k = (round(x * inv), round(y * inv), round(z * inv))
        if k not in key:
            key[k] = len(key)
            pos.append((x, y, z))
        remap.append(key[k])
    wf = []
    for f in faces:
        w = [remap[i] for i in f]
        if len(set(w)) >= 3:
            wf.append(w)
    return pos, wf


def box(pos, vs):
    xs = [pos[i][0] for i in vs]
    ys = [pos[i][1] for i in vs]
    zs = [pos[i][2] for i in vs]
    b = (max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))
    return b, math.dist((0, 0, 0), b)


def tri_area(a, b, c):
    u = [b[i] - a[i] for i in range(3)]
    v = [c[i] - a[i] for i in range(3)]
    w = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
         u[0] * v[1] - u[1] * v[0])
    return 0.5 * math.dist((0, 0, 0), w)


def analyse(path, tol):
    verts, faces = meshpy.read_mesh(path)
    pos, wf = weld(verts, faces, tol)

    allv = set(range(len(pos)))
    whole_box, whole_diag = box(pos, allv)

    edge_faces = collections.defaultdict(list)
    at_vert = collections.defaultdict(lambda: collections.defaultdict(list))
    vfaces = collections.defaultdict(set)
    for fi, w in enumerate(wf):
        n = len(w)
        for i in range(n):
            a, b = w[i], w[(i + 1) % n]
            if a == b:
                continue
            ek = (min(a, b), max(a, b))
            edge_faces[ek].append(fi)
            at_vert[a][ek].append(fi)
            at_vert[b][ek].append(fi)
        for a in set(w):
            vfaces[a].add(fi)

    # --- components -----------------------------------------------------
    fadj = collections.defaultdict(set)
    for ek, fs in edge_faces.items():
        for i in range(1, len(fs)):
            fadj[fs[0]].add(fs[i])
            fadj[fs[i]].add(fs[0])
    seen, comps = set(), []
    for s in range(len(wf)):
        if s in seen:
            continue
        stack, mem = [s], []
        seen.add(s)
        while stack:
            c = stack.pop()
            mem.append(c)
            for n in fadj[c]:
                if n not in seen:
                    seen.add(n)
                    stack.append(n)
        comps.append(mem)

    parts = []
    for mem in comps:
        vs = set()
        for fi in mem:
            vs.update(wf[fi])
        b, d = box(pos, vs)
        vol = 0.0
        for fi in mem:
            w = wf[fi]
            for i in range(1, len(w) - 1):
                p, q, r = pos[w[0]], pos[w[i]], pos[w[i + 1]]
                vol += (p[0] * (q[1] * r[2] - q[2] * r[1])
                        - p[1] * (q[0] * r[2] - q[2] * r[0])
                        + p[2] * (q[0] * r[1] - q[1] * r[0])) / 6.0
        area = sum(tri_area(pos[wf[fi][0]], pos[wf[fi][i]], pos[wf[fi][i + 1]])
                   for fi in mem for i in range(1, len(wf[fi]) - 1))
        parts.append({'f': len(mem), 'v': len(vs), 'box': b, 'diag': d,
                      'vol': abs(vol), 'area': area,
                      'centre': tuple(sum(pos[i][k] for i in vs) / len(vs)
                                      for k in range(3))})
    parts.sort(key=lambda p: p['diag'])

    # --- pinches, and the patch each one carries -------------------------
    # Face adjacency again, but keyed by the pair so the pinch vertex can be
    # cut out of it: growing a fan through edges that do NOT touch the pinch is
    # what separates the patch hanging off the point from the solid it hangs on.
    pair_edges = collections.defaultdict(list)
    for ek, fs in edge_faces.items():
        for i in range(len(fs)):
            for j in range(i + 1, len(fs)):
                pair_edges[(min(fs[i], fs[j]), max(fs[i], fs[j]))].append(ek)

    pinches = []
    for pv, edges in at_vert.items():
        inc = vfaces[pv]
        if len(inc) < 2:
            continue
        uf = meshpy.Find()
        for ek, fs in edges.items():
            for other in fs[1:]:
                uf.union(fs[0], other)
        fans = uf.groups(inc)
        if len(fans) < 2:
            continue
        best = None
        for fan in fans:
            reach, stack = set(fan), list(fan)
            while stack:
                c = stack.pop()
                for n in fadj[c]:
                    if n in reach:
                        continue
                    shared = pair_edges[(min(c, n), max(c, n))]
                    if all(pv in ek for ek in shared):
                        continue          # only touches through the pinch
                    reach.add(n)
                    stack.append(n)
            vs = set()
            for fi in reach:
                vs.update(wf[fi])
            b, d = box(pos, vs)
            cand = {'f': len(reach), 'box': b, 'diag': d}
            if best is None or d < best['diag']:
                best = cand
        pinches.append({'pos': pos[pv], 'fans': len(fans), 'faces': len(inc),
                        'patch': best})
    pinches.sort(key=lambda p: p['patch']['diag'])

    # --- non-manifold edges ---------------------------------------------
    nm = [(ek, fs) for ek, fs in edge_faces.items() if len(fs) > 2]
    nm_report = {'n': len(nm)}
    if nm:
        lens = sorted(math.dist(pos[a], pos[b]) for (a, b), _ in nm)
        vs = set()
        inc_faces = set()
        for (a, b), fs in nm:
            vs.add(a)
            vs.add(b)
            inc_faces.update(fs)
        b_, d_ = box(pos, vs)
        # cluster the non-manifold edges by shared vertex
        uf = meshpy.Find()
        for (a, b), _ in nm:
            uf.union(a, b)
        cl = collections.defaultdict(set)
        for (a, b), _ in nm:
            cl[uf.find(a)].update((a, b))
        clusters = []
        for k, vv in cl.items():
            bb, dd = box(pos, vv)
            clusters.append({'v': len(vv), 'box': bb, 'diag': dd})
        clusters.sort(key=lambda c: c['diag'])
        tris = sorted((tri_area(pos[wf[fi][0]], pos[wf[fi][1]], pos[wf[fi][2]]), fi)
                      for fi in inc_faces if len(wf[fi]) == 3)
        smallest = []
        for ar, fi in tris[:4]:
            bb, dd = box(pos, set(wf[fi]))
            smallest.append({'area': ar, 'box': bb, 'diag': dd})
        # exact duplicate triangles, same three vertices, either orientation
        seen_tri = collections.defaultdict(list)
        for fi in inc_faces:
            seen_tri[tuple(sorted(wf[fi]))].append(fi)
        dups = []
        for k, fs in seen_tri.items():
            if len(fs) > 1:
                bb, dd = box(pos, set(k))
                dups.append({'n': len(fs), 'diag': dd,
                             'area': tri_area(pos[k[0]], pos[k[1]], pos[k[2]])})
        nm_report.update({
            'edge_len_min': lens[0], 'edge_len_max': lens[-1],
            'locus_box': b_, 'locus_diag': d_,
            'clusters': clusters, 'faces': len(inc_faces),
            'smallest_tri': smallest, 'duplicate_pairs': dups,
        })

    return {'file': path, 'tol': tol, 'v': len(pos), 'f': len(wf),
            'model_box': whole_box, 'model_diag': whole_diag,
            'comp': len(comps), 'parts': parts,
            'pinch': pinches, 'nonman': nm_report}


def mm(x):
    if x == 0:
        return '0'
    if x >= 0.1:
        return f'{x:.4g} mm'
    if x >= 1e-4:
        return f'{x * 1000:.4g} um'
    return f'{x * 1e6:.4g} nm'


def show(r):
    print(f"\n== {os.path.basename(r['file'])}  v={r['v']} f={r['f']} "
          f"comp={r['comp']}  tol={r['tol']:g}")
    b = r['model_box']
    print(f"   model  {b[0]:.4g} x {b[1]:.4g} x {b[2]:.4g} mm, "
          f"diag {r['model_diag']:.4g} mm")
    if r['comp'] > 1:
        for p in r['parts'][:-1]:
            b = p['box']
            print(f"   PART   {p['f']}f {p['v']}v  "
                  f"{b[0]:.4g} x {b[1]:.4g} x {b[2]:.4g} mm  diag {mm(p['diag'])}"
                  f"  vol {p['vol']:.3g} mm^3  at "
                  f"({p['centre'][0]:.6g},{p['centre'][1]:.6g},{p['centre'][2]:.6g})")
    for p in r['pinch']:
        q = p['patch']
        b = q['box']
        print(f"   PINCH  {p['fans']} fans of {p['faces']} faces at "
              f"({p['pos'][0]:.10g},{p['pos'][1]:.10g},{p['pos'][2]:.10g})")
        print(f"          patch {q['f']}f  {b[0]:.4g} x {b[1]:.4g} x {b[2]:.4g} mm"
              f"  diag {mm(q['diag'])}")
    n = r['nonman']
    if n['n']:
        print(f"   NONMAN {n['n']} edges on {n['faces']} faces; "
              f"edge length {mm(n['edge_len_min'])} .. {mm(n['edge_len_max'])}")
        b = n['locus_box']
        print(f"          locus {b[0]:.4g} x {b[1]:.4g} x {b[2]:.4g} mm, "
              f"diag {mm(n['locus_diag'])}, {len(n['clusters'])} cluster(s): "
              + ', '.join(mm(c['diag']) for c in n['clusters']))
        for t in n['smallest_tri'][:2]:
            b = t['box']
            print(f"          smallest incident triangle: area {t['area']:.4g} mm^2, "
                  f"{b[0]:.3g} x {b[1]:.3g} x {b[2]:.3g} mm, diag {mm(t['diag'])}")
        if n['duplicate_pairs']:
            print(f"          DUPLICATE TRIANGLES: {len(n['duplicate_pairs'])} sets "
                  + ', '.join(f"{d['n']}x diag {mm(d['diag'])} area {d['area']:.3g} mm^2"
                              for d in n['duplicate_pairs'])
                  + '  -- zero-thickness membrane, no thickness to report')


def check(stldir, tol):
    """The standing rule: known answers before any new reading is believed."""
    ok = True

    def say(good, what):
        nonlocal ok
        print(('  PASS  ' if good else '  FAIL  ') + what)
        ok = ok and good

    r = analyse(os.path.join(stldir, 'tee_small_fndef_rdef.stl'), tol)
    p = r['parts'][0]
    b = sorted(p['box'])
    say(p['f'] == 6 and abs(b[0] - 0.10) < 0.02 and abs(b[1] - 0.35) < 0.02
        and abs(b[2] - 0.40) < 0.02,
        f"tee_small defaults: 6-triangle part 0.35 x 0.10 x 0.40 mm "
        f"(read {p['f']}f {b[0]:.3g} x {b[1]:.3g} x {b[2]:.3g})")

    r = analyse(os.path.join(stldir, 'cross_fndef_r0.9.stl'), tol)
    p = r['parts'][0]
    say(p['f'] == 4 and abs(p['vol'] - 3.45e-7) < 5e-8,
        f"cross r=0.9: 4-triangle shard of 3.45e-7 mm^3 "
        f"(read {p['f']}f {p['vol']:.3g})")

    r = analyse(os.path.join(stldir, 'tee_fndef_r0.9.stl'), tol)
    want = (1.913417162, -4.619397663, 8.086582838)
    say(len(r['pinch']) == 1
        and math.dist(r['pinch'][0]['pos'], want) < 1e-6,
        f"tee r=0.9: one pinch at {want} (read {r['pinch'][0]['pos'] if r['pinch'] else None})")

    r = analyse(os.path.join(stldir, 'refused_neighbour_fndef_r0.9.stl'), tol)
    n = r['nonman']
    say(n['n'] == 1 and n['faces'] == 4 and 1e-4 < n['locus_diag'] < 1e-3,
        f"refused_neighbour r=0.9: one non-manifold edge on 4 faces, "
        f"sub-micron ({n['n']} edges, {n['faces']} faces, "
        f"locus {mm(n['locus_diag'])})")

    print('REMNANT.PY CHECK ' + ('PASSED' if ok else 'FAILED')
          + f' -- 4 known answers, weld {tol:g}.')
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='*')
    ap.add_argument('--tol', type=float, default=1e-6)
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--check', metavar='STLDIR',
                    help='run the four known answers in a directory of the '
                         'S1 renders and exit')
    a = ap.parse_args()
    if a.check:
        sys.exit(check(a.check, a.tol))
    out = [analyse(f, a.tol) for f in a.files]
    if a.json:
        print(json.dumps(out, indent=1))
    else:
        for r in out:
            show(r)


if __name__ == '__main__':
    main()
