#!/usr/bin/env python3
"""Convex-crease census on a mesh, matching FilletBuilder::classifyEdge exactly.

dihedral = angle between the two face normals (0 = flat, 90 = right angle)
concave  = A's far corner pokes in front of B's plane (outward normals)
"""
import sys, math, collections

def read_stl(path):
    verts, faces, idx, cur = [], [], {}, []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line.startswith('vertex'):
                t = tuple(float(x) for x in line.split()[1:4])
                if t not in idx:
                    idx[t] = len(verts); verts.append(t)
                cur.append(idx[t])
            elif line.startswith('endloop'):
                if len(cur) >= 3: faces.append(cur)
                cur = []
    if not faces: raise ValueError(path + ': no facets')
    return verts, faces

def weld(verts, faces, tol):
    if tol <= 0:
        return verts, faces
    q = {}
    remap = []
    out = []
    inv = 1.0 / tol
    for v in verts:
        k = tuple(int(math.floor(c * inv + 0.5)) for c in v)
        hit = None
        for dk in ((0,0,0),):
            kk = (k[0]+dk[0], k[1]+dk[1], k[2]+dk[2])
            if kk in q: hit = q[kk]; break
        if hit is None:
            # also probe neighbours so a pair straddling a cell boundary welds
            for dx in (-1,0,1):
                for dy in (-1,0,1):
                    for dz in (-1,0,1):
                        kk = (k[0]+dx, k[1]+dy, k[2]+dz)
                        if kk in q:
                            w = out[q[kk]]
                            if max(abs(w[i]-v[i]) for i in range(3)) <= tol:
                                hit = q[kk]; break
                    if hit is not None: break
                if hit is not None: break
        if hit is None:
            hit = len(out); out.append(v); q[k] = hit
        remap.append(hit)
    nf = []
    for f in faces:
        g = [remap[i] for i in f]
        h = [g[0]]
        for x in g[1:]:
            if x != h[-1]: h.append(x)
        if len(h) > 1 and h[0] == h[-1]: h.pop()
        if len(h) >= 3: nf.append(h)
    return out, nf

def sub(a,b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def cross(a,b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def dot(a,b): return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]
def norm(a):
    n = math.sqrt(dot(a,a))
    return (a[0]/n, a[1]/n, a[2]/n) if n > 0 else (0.0,0.0,0.0)

def triangulate(faces):
    out = []
    for f in faces:
        for i in range(1, len(f)-1):
            out.append((f[0], f[i], f[i+1]))
    return out

def census(path, tol=1e-7):
    verts, faces = read_stl(path)
    verts, faces = weld(verts, faces, tol)
    tris = triangulate(faces)
    nrm = []
    for t in tris:
        a,b,c = verts[t[0]], verts[t[1]], verts[t[2]]
        nrm.append(norm(cross(sub(b,a), sub(c,a))))
    adj = collections.defaultdict(list)
    for i,t in enumerate(tris):
        for k in range(3):
            e = (t[k], t[(k+1)%3])
            adj[(min(e), max(e))].append(i)
    edges = []
    nonman = 0
    for key, ts in adj.items():
        if len(ts) != 2:
            nonman += 1; continue
        A, B = ts[0], ts[1]
        d = max(-1.0, min(1.0, dot(nrm[A], nrm[B])))
        phi = math.degrees(math.acos(d))
        aFar = None
        for k in tris[A]:
            if k != key[0] and k != key[1]: aFar = k; break
        if aFar is None: continue
        concave = dot(nrm[B], sub(verts[aFar], verts[key[0]])) > 0
        p0, p1 = verts[key[0]], verts[key[1]]
        mid = tuple((p0[i]+p1[i])/2 for i in range(3))
        ln = math.sqrt(dot(sub(p1,p0), sub(p1,p0)))
        edges.append(dict(phi=phi, concave=concave, mid=mid, p0=p0, p1=p1, length=ln))
    return dict(verts=verts, tris=tris, nrm=nrm, edges=edges, nonman=nonman, adj=adj)

if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('path')
    ap.add_argument('--tol', type=float, default=1e-7)
    ap.add_argument('--thr', type=float, default=46.0)
    a = ap.parse_args()
    r = census(a.path, a.tol)
    cv = [e for e in r['edges'] if not e['concave']]
    cc = [e for e in r['edges'] if e['concave']]
    print(f"{a.path}: v={len(r['verts'])} t={len(r['tris'])} e={len(r['edges'])} nonman={r['nonman']}")
    for name, s in (('convex', cv), ('concave', cc)):
        steep = [e for e in s if e['phi'] > a.thr]
        print(f"  {name}: {len(s)} total, {len(steep)} above {a.thr} deg")
        for e in sorted(steep, key=lambda x: -x['phi'])[:200]:
            m = e['mid']
            print(f"    {e['phi']:8.3f} deg  len={e['length']:.6f}  at ({m[0]:9.5f},{m[1]:9.5f},{m[2]:9.5f})")
