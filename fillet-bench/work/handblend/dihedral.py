#!/usr/bin/env python3
"""Dihedral angle at every welded edge of an ASCII STL, filtered by location.

Reports the signed-free dihedral (0 = coplanar) for edges whose midpoint falls
in a box, so "the crease at the blend boundary" can be asked for by name.
"""
import sys, math, collections, argparse

def read_stl(path):
    verts, faces, idx, cur = [], [], {}, []
    for line in open(path):
        s = line.split()
        if s and s[0] == 'vertex':
            t = tuple(float(x) for x in s[1:4]); cur.append(t)
        elif s and s[0] == 'endloop':
            faces.append(tuple(cur)); cur = []
    return faces

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('stl'); ap.add_argument('--tol', type=float, default=1e-6)
    ap.add_argument('--box', nargs=6, type=float, default=None,
                    help='xlo xhi ylo yhi zlo zhi on the edge midpoint')
    ap.add_argument('--min', type=float, default=1e-7, help='only print above this')
    ap.add_argument('--top', type=int, default=20)
    a = ap.parse_args()
    q = lambda p: tuple(round(c/a.tol) for c in p)
    tris = read_stl(a.stl)
    pos = {}
    idx = []
    for t in tris:
        f = []
        for p in t:
            k = q(p)
            if k not in pos: pos[k] = p
            f.append(k)
        idx.append(f)
    normals = []
    for f in idx:
        A,B,C = (pos[k] for k in f)
        u = [B[i]-A[i] for i in range(3)]; v = [C[i]-A[i] for i in range(3)]
        n = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
        m = math.sqrt(sum(c*c for c in n)) or 1.0
        normals.append([c/m for c in n])
    edge = collections.defaultdict(list)
    for i,f in enumerate(idx):
        for k in range(3):
            e = tuple(sorted((f[k], f[(k+1)%3])))
            edge[e].append(i)
    out = []
    for e,ts in edge.items():
        if len(ts) != 2: continue
        d = sum(normals[ts[0]][i]*normals[ts[1]][i] for i in range(3))
        ang = math.degrees(math.acos(max(-1.0,min(1.0,d))))
        p0, p1 = pos[e[0]], pos[e[1]]
        mid = [(p0[i]+p1[i])/2 for i in range(3)]
        if a.box:
            b = a.box
            if not (b[0]<=mid[0]<=b[1] and b[2]<=mid[1]<=b[3] and b[4]<=mid[2]<=b[5]): continue
        if ang < a.min: continue
        out.append((ang, mid))
    out.sort(reverse=True)
    print(f"tol={a.tol} edges_in_box_above_{a.min}deg={len(out)}")
    for ang, mid in out[:a.top]:
        print(f"  {ang:9.4f} deg at [{mid[0]:.5f}, {mid[1]:.5f}, {mid[2]:.5f}]")
main()
