#!/usr/bin/env python3
"""Signed step height of a blended mesh against the ORIGINAL target's wall planes.

Reads the target mesh (no fillet) and the blended mesh, extracts the target's
distinct face planes, and for every steep convex edge on the blended mesh
reports the signed distance of its midpoint to the nearest target plane whose
normal one of the edge's own faces shares. Positive = outside the wall (proud),
negative = inside it (sunk).
"""
import sys, math, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from census import census, sub, dot, norm
from relief import analyse

def planes_of(path, tol=1e-7):
    r = census(path, tol)
    verts, tris, nrm = r['verts'], r['tris'], r['nrm']
    ps = {}
    for i, t in enumerate(tris):
        n = nrm[i]
        d = dot(n, verts[t[0]])
        k = (round(n[0], 7), round(n[1], 7), round(n[2], 7), round(d, 7))
        ps.setdefault(k, (n, d))
    return list(ps.values())

def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('target'); ap.add_argument('blend')
    ap.add_argument('--tol', type=float, default=1e-7)
    ap.add_argument('--thr', type=float, default=46.0)
    ap.add_argument('--nmatch', type=float, default=1e-4,
                    help='max 1-|cos| between a face normal and a target plane normal')
    a = ap.parse_args()
    P = planes_of(a.target, a.tol)
    r, es = analyse(a.blend, a.tol, a.thr)
    print(f"target planes: {len(P)}   steep convex edges on blend: {len(es)}")
    rows = []
    for e in es:
        A, B = e['tris']
        best = None
        for ti in (A, B):
            n = r['nrm'][ti]
            for (pn, pd) in P:
                c = dot(n, pn)
                if 1.0 - c > a.nmatch: continue
                s = dot(pn, e['mid']) - pd
                if best is None or abs(s) < abs(best[0]): best = (s, ti)
        rows.append((e, best))
    hist = collections.Counter()
    for e, b in rows:
        if b is None: hist['no-wall-face'] += 1
        else:
            s = b[0]
            m = abs(s)
            tag = 'zero' if m < 1e-12 else f"{'+' if s>0 else '-'}1e{int(math.floor(math.log10(m)))}"
            hist[tag] += 1
    print('  signed offset to nearest matching target plane, decades:', dict(sorted(hist.items())))
    print('  relief decades:', dict(sorted(collections.Counter(
        ('0' if e['relief'] == 0 else f"1e{int(math.floor(math.log10(e['relief'])))}")
        for e in es).items())))
    for e, b in sorted(rows, key=lambda x: -x[0]['phi']):
        m = e['mid']
        off = 'n/a' if b is None else f"{b[0]:+.9g}"
        print(f"    dih={e['phi']:8.3f} relief={e['relief']:.6g} off={off:>14} "
              f"at ({m[0]:9.5f},{m[1]:9.5f},{m[2]:9.5f})")

if __name__ == '__main__':
    main()
