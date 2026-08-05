#!/usr/bin/env python3
"""Attempt 2's rule (mesh-seated ball + angular residual) on the SAME steep
convex edge population agent-ridge/battery.py measured for STATE.md 4d.

Port of FilletBuilder.cc as of 042bf9dff: reseat() (Newton on the two
distances-to-mesh, held in the crease section plane) then seatOn()'s
r*sin(angle from contact->centre direction to the mesh's normal cone at the
contact).
"""
import sys, math, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from census import census, sub, dot, cross, norm


def planes_of(path, tol=1e-7):
    """The target's distinct face planes, as agent-ridge/step.py had it."""
    r = census(path, tol)
    ps = {}
    for i, t in enumerate(r['tris']):
        n = r['nrm'][i]
        d = dot(n, r['verts'][t[0]])
        ps.setdefault((round(n[0], 7), round(n[1], 7), round(n[2], 7), round(d, 7)), (n, d))
    return list(ps.values())

TURN_CAP = math.cos(math.radians(60.0))   # kWallTurnDeg
SEAT_ITERS = 12


def closest_on_tri(p, a, b, c):
    ab = sub(b, a); ac = sub(c, a); ap = sub(p, a)
    d1 = dot(ab, ap); d2 = dot(ac, ap)
    if d1 <= 0 and d2 <= 0: return a
    bp = sub(p, b); d3 = dot(ab, bp); d4 = dot(ac, bp)
    if d3 >= 0 and d4 <= d3: return b
    vc = d1*d4 - d3*d2
    if vc <= 0 and d1 >= 0 and d3 <= 0:
        v = d1/(d1-d3) if d1 != d3 else 0.0
        return tuple(a[i]+v*ab[i] for i in range(3))
    cp = sub(p, c); d5 = dot(ab, cp); d6 = dot(ac, cp)
    if d6 >= 0 and d5 <= d6: return c
    vb = d5*d2 - d1*d6
    if vb <= 0 and d2 >= 0 and d6 <= 0:
        w = d2/(d2-d6) if d2 != d6 else 0.0
        return tuple(a[i]+w*ac[i] for i in range(3))
    va = d3*d6 - d5*d4
    if va <= 0 and (d4-d3) >= 0 and (d5-d6) >= 0:
        w = (d4-d3)/((d4-d3)+(d5-d6))
        return tuple(b[i]+w*(c[i]-b[i]) for i in range(3))
    den = 1.0/(va+vb+vc); v = vb*den; w = vc*den
    return tuple(a[i]+ab[i]*v+ac[i]*w for i in range(3))


def inv3(M):
    (a, b, c), (d, e, f), (g, h, i) = M
    det = a*(e*i-f*h) - b*(d*i-f*g) + c*(d*h-e*g)
    if abs(det) < 1e-6: return None, det
    r = 1.0/det
    return [[(e*i-f*h)*r, (c*h-b*i)*r, (b*f-c*e)*r],
            [(f*g-d*i)*r, (a*i-c*g)*r, (c*d-a*f)*r],
            [(d*h-e*g)*r, (b*g-a*h)*r, (a*e-b*d)*r]], det


def mv(M, v):
    return tuple(sum(M[k][j]*v[j] for j in range(3)) for k in range(3))


def angle_between(a, b):
    return math.atan2(math.dist(cross(a, b), (0, 0, 0)), dot(a, b))


class Mesh:
    nosurf = False

    def __init__(self, path, tol=1e-7, thr=46.0):
        r = census(path, tol)
        self.verts, self.tris, self.nrm, self.adj = r['verts'], r['tris'], r['nrm'], r['adj']
        self.census = r
        # feature edges bound a smooth surface: the same rule seat.py uses
        feat = set()
        for key, ts in self.adj.items():
            if len(ts) != 2: feat.add(key); continue
            A, B = ts
            d = max(-1.0, min(1.0, dot(self.nrm[A], self.nrm[B])))
            if math.degrees(math.acos(d)) > thr: feat.add(key)
        self.feat = feat
        surf = [-1]*len(self.tris)
        sid = 0
        for i in range(len(self.tris)):
            if surf[i] >= 0: continue
            stack = [i]; surf[i] = sid
            while stack:
                t = stack.pop()
                for k in range(3):
                    e = (min(self.tris[t][k], self.tris[t][(k+1) % 3]),
                         max(self.tris[t][k], self.tris[t][(k+1) % 3]))
                    if e in feat: continue
                    for nb in self.adj[e]:
                        if surf[nb] < 0: surf[nb] = sid; stack.append(nb)
            sid += 1
        self.surf = surf

    def nearest_on_wall(self, p, frm, start_tri, surface, budget, want_tied=False):
        """FilletBuilder.cc's nearestOnWall."""
        best = math.inf; bestq = p; besttri = -1
        nearby = []
        seen = {start_tri}; stack = [start_tri]
        n0 = self.nrm[start_tri]
        while stack:
            t = stack.pop()
            tri = self.tris[t]
            a, b, c = self.verts[tri[0]], self.verts[tri[1]], self.verts[tri[2]]
            q = closest_on_tri(p, a, b, c)
            if want_tied: nearby.append((q, t))
            dq = math.dist(p, q)
            if dq < best: best = dq; bestq = q; besttri = t
            if math.dist(closest_on_tri(frm, a, b, c), frm) > budget: continue
            for k in range(3):
                e = (min(tri[k], tri[(k+1) % 3]), max(tri[k], tri[(k+1) % 3]))
                for nb in self.adj.get(e, ()):
                    if (self.nosurf or self.surf[nb] == surface) \
                       and dot(self.nrm[nb], n0) > TURN_CAP and nb not in seen:
                        seen.add(nb); stack.append(nb)
        tied = []
        if want_tied:
            join = 1e-6*max(1.0, best)
            tied = [t for (q, t) in nearby if math.dist(q, bestq) <= join]
        return best, bestq, besttri, tied

    def angle_to_cone(self, u, tris):
        best = math.pi
        for t in tris:
            best = min(best, angle_between(u, self.nrm[t]))
        for i in range(len(tris)):
            for j in range(i+1, len(tris)):
                na, nb = self.nrm[tris[i]], self.nrm[tris[j]]
                ax = cross(na, nb)
                if math.dist(ax, (0, 0, 0)) < 1e-12: continue
                ax = norm(ax)
                proj = tuple(u[k] - dot(u, ax)*ax[k] for k in range(3))
                if math.dist(proj, (0, 0, 0)) < 1e-12: continue
                proj = norm(proj)
                span = dot(na, nb)
                if dot(proj, na) < span - 1e-12 or dot(proj, nb) < span - 1e-12: continue
                best = min(best, angle_between(u, proj))
        return best


def residual(mesh, key, A, B, R, dirs):
    """dirs = -1 convex (round tool), +1 concave. Returns (off, info)."""
    nA, nB = mesh.nrm[A], mesh.nrm[B]
    cosphi = max(-1.0, min(1.0, dot(nA, nB)))
    phi = math.acos(cosphi)
    bis = tuple(nA[i]+nB[i] for i in range(3))
    if math.degrees(phi) > 179.0 or math.dist(bis, (0, 0, 0)) < 1e-9: return None, {}
    bis = norm(bis)
    p0, p1 = mesh.verts[key[0]], mesh.verts[key[1]]
    v = tuple((p0[i]+p1[i])/2 for i in range(3))
    C = tuple(v[i] + dirs*(R/math.cos(phi/2))*bis[i] for i in range(3))
    sA, sB = mesh.surf[A], mesh.surf[B]

    # reseat()
    t = cross(nA, nB)
    C0 = C
    iters = 0
    if math.dist(t, (0, 0, 0)) >= 1e-9:
        t = norm(t)
        for it in range(SEAT_ITERS):
            iters = it+1
            budget = math.dist(C, v) + R
            dA, pA, _, _ = mesh.nearest_on_wall(C, v, A, sA, budget)
            dB, pB, _, _ = mesh.nearest_on_wall(C, v, B, sB, budget)
            if not (math.isfinite(dA) and math.isfinite(dB)): break
            if dA < 1e-12 or dB < 1e-12: break
            M = [[(C[i]-pA[i])/dA for i in range(3)],
                 [(C[i]-pB[i])/dB for i in range(3)],
                 list(t)]
            Mi, det = inv3(M)
            if Mi is None: C = C0; break
            step = mv(Mi, (R-dA, R-dB, 0.0))
            sn = math.dist(step, (0, 0, 0))
            lim = 0.5*R
            if sn > lim: step = tuple(x*lim/sn for x in step); sn = lim
            C = tuple(C[i]+step[i] for i in range(3))
            if sn < 1e-12*max(1.0, R): break

    # seatOn() on each side
    off = 0.0
    info = {'iters': iters, 'C': C}
    for (tri, s) in ((A, sA), (B, sB)):
        budget = math.dist(C, v) + R
        d, onwall, ontri, tied = mesh.nearest_on_wall(C, v, tri, s, budget, want_tied=True)
        if not math.isfinite(d) or d < 1e-12 or not tied: continue
        u = tuple(dirs*(C[i]-onwall[i])/d for i in range(3))
        ang = mesh.angle_to_cone(u, tied)
        off = max(off, R*math.sin(ang))
        info.setdefault('d', []).append(d)
        info.setdefault('ang', []).append(ang)
    return off, info


def main():
    target, blend, R, lab = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4]
    seg = float(sys.argv[5]) if len(sys.argv) > 5 else 46.0
    want_concave = len(sys.argv) > 6 and sys.argv[6] == 'concave'
    lab = f'{lab} seg={seg} {"concave" if want_concave else "convex"}'
    dirs = 1.0 if want_concave else -1.0
    P = planes_of(target)
    mesh = Mesh(blend, thr=seg)
    if len(sys.argv) > 7 and sys.argv[7] == 'nosurf':
        mesh.nosurf = True
        lab += ' nosurf'

    def inplane(ti, mid):
        n = mesh.nrm[ti]
        for (pn, pd) in P:
            if 1.0 - dot(n, pn) <= 1e-4 and abs(dot(pn, mid)-pd) < 1e-9: return True
        return False

    gen, art = [], []
    for key, ts in mesh.adj.items():
        if len(ts) != 2: continue
        A, B = ts
        d = max(-1.0, min(1.0, dot(mesh.nrm[A], mesh.nrm[B])))
        phi = math.degrees(math.acos(d))
        if phi <= 46.0: continue
        aFar = next((k for k in mesh.tris[A] if k not in key), None)
        if aFar is None: continue
        concave = dot(mesh.nrm[B], sub(mesh.verts[aFar], mesh.verts[key[0]])) > 0
        if concave != want_concave: continue
        p0, p1 = mesh.verts[key[0]], mesh.verts[key[1]]
        mid = tuple((p0[i]+p1[i])/2 for i in range(3))
        off, info = residual(mesh, key, A, B, R, dirs)
        if off is None: continue
        row = dict(key=key, phi=phi, mid=mid, off=off, info=info)
        (gen if (inplane(A, mid) and inplane(B, mid)) else art).append(row)

    def q(xs):
        xs = sorted(xs); n = len(xs)
        return f'{xs[0]:.5g} / {xs[n//2]:.5g} / {xs[-1]:.5g}' if n else '-'
    print(f'== {lab}  R={R}  genuine={len(gen)}  blend-made={len(art)}')
    for name, s in (('GENUINE', gen), ('BLEND-MADE', art)):
        if not s: print(f'   {name}: 0'); continue
        o = [x['off'] for x in s]
        print(f'   {name}: n={len(s)}  angular residual   min/med/max: {q(o)}')
        print(f'      /R  min/med/max: {q([x/R for x in o])}')
        print(f'      caught (>1e-9*max(1,R)): {sum(1 for x in o if x > 1e-9*max(1.0, R))}/{len(s)}')
        print(f'      seat iters max: {max(x["info"]["iters"] for x in s)}')
    if gen and art:
        gmax = max(x['off'] for x in gen); amin = min(x['off'] for x in art)
        print(f'   >>> genuine MAX = {gmax:.6g}   blend-made MIN = {amin:.6g}   '
              f'separation = {amin/gmax if gmax > 0 else float("inf"):.4g}x')


if __name__ == '__main__':
    main()
