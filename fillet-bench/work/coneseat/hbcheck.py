#!/usr/bin/env python3
"""Known-answer check for the cone.py port.

handblend_step's convex test edge is at (0, H) with H = R + D, running along z.
STATE.md 4d records the C++ rule printing R(1-tan(DELTA/2)) - D = 0.934457 - D
at min_angle=5 (blend not merged into the wall) and 8.9e-16 at the default 46
(blend merged).  DELTA = 7.5 deg at $fn=48.
"""
import sys, math
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from cone import Mesh, residual
from census import dot

R = 1.0
for D in (0.02, 0.5, 0.9344, 0.95):
    H = R + D
    for thr in (5.0, 46.0):
        mesh = Mesh(f'{sys.argv[1]}/hb_plain_{D}.stl', thr=thr)
        offs = []
        for key, ts in mesh.adj.items():
            if len(ts) != 2: continue
            p0, p1 = mesh.verts[key[0]], mesh.verts[key[1]]
            # the test edge: x=0, y=H, running in z
            if not (abs(p0[0]) < 1e-9 and abs(p1[0]) < 1e-9): continue
            if not (abs(p0[1]-H) < 1e-6 and abs(p1[1]-H) < 1e-6): continue
            mid = tuple((p0[i]+p1[i])/2 for i in range(3))
            if mid[2] < 4.0 or mid[2] > 16.0: continue   # away from the two end junctions
            off, info = residual(mesh, key, ts[0], ts[1], R, -1.0)
            if off is not None: offs.append((off, mid[2], info))
        if offs:
            o = sorted(x[0] for x in offs)
            print(f'D={D:<7} thr={thr:<5} n={len(offs)}  off min/med/max = '
                  f'{o[0]:.6g} / {o[len(o)//2]:.6g} / {o[-1]:.6g}   '
                  f'expected(thr=5) {max(0.934457-D,0):.6g}')
        else:
            print(f'D={D} thr={thr}: no edge found')
