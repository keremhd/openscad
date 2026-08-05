#!/usr/bin/env python3
"""Steep-convex-edge census with local RELIEF, i.e. how tall the ridge actually is.

For each convex edge above the threshold, both adjacent triangles are taken and
the perpendicular distance from each triangle's far corner to the OTHER
triangle's plane is measured. The smaller of the two is the relief: the height
of the step the edge represents, measured over the extent of one triangle.

A cube's edge has a relief of a whole side. A bead standing proud of the wall it
should be tangent to by delta has a relief of delta, whatever the dihedral says.

Validated on cube (relief == side length) and on an $fn=8 cylinder's wall seams.
"""
import sys, math, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from census import census, sub, dot, cross, norm

def analyse(path, tol=1e-7, thr=46.0):
    r = census(path, tol)
    verts, tris, nrm, adj = r['verts'], r['tris'], r['nrm'], r['adj']
    out = []
    for key, ts in adj.items():
        if len(ts) != 2: continue
        A, B = ts
        d = max(-1.0, min(1.0, dot(nrm[A], nrm[B])))
        phi = math.degrees(math.acos(d))
        aFar = next((k for k in tris[A] if k not in key), None)
        bFar = next((k for k in tris[B] if k not in key), None)
        if aFar is None or bFar is None: continue
        concave = dot(nrm[B], sub(verts[aFar], verts[key[0]])) > 0
        if concave or phi <= thr: continue
        # far corner of A above B's plane, and vice versa
        hA = abs(dot(nrm[B], sub(verts[aFar], verts[key[0]])))
        hB = abs(dot(nrm[A], sub(verts[bFar], verts[key[0]])))
        p0, p1 = verts[key[0]], verts[key[1]]
        mid = tuple((p0[i] + p1[i]) / 2 for i in range(3))
        ln = math.sqrt(dot(sub(p1, p0), sub(p1, p0)))
        out.append(dict(phi=phi, relief=min(hA, hB), hA=hA, hB=hB, mid=mid,
                        length=ln, key=key, tris=(A, B)))
    return r, out

if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('path'); ap.add_argument('--tol', type=float, default=1e-7)
    ap.add_argument('--thr', type=float, default=46.0)
    ap.add_argument('--full', action='store_true')
    a = ap.parse_args()
    r, es = analyse(a.path, a.tol, a.thr)
    print(f"{a.path}: v={len(r['verts'])} t={len(r['tris'])} nonman={r['nonman']}  "
          f"steep convex (> {a.thr} deg): {len(es)}")
    # relief histogram, decades
    buckets = collections.Counter()
    for e in es:
        h = e['relief']
        b = '0' if h == 0 else f"1e{int(math.floor(math.log10(h)))}"
        buckets[b] += 1
    print('  relief decades:', dict(sorted(buckets.items())))
    for e in sorted(es, key=lambda x: x['relief']):
        m = e['mid']
        print(f"    dih={e['phi']:8.3f} relief={e['relief']:.9g} len={e['length']:.6g} "
              f"at ({m[0]:9.5f},{m[1]:9.5f},{m[2]:9.5f})")
        if not a.full and len(es) > 60 and e is sorted(es, key=lambda x: x['relief'])[59]:
            print('    ...'); break
