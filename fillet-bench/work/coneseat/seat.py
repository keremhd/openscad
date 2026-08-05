#!/usr/bin/env python3
"""Ball-seating and relief analysis of the steep convex creases on a mesh.

For every convex edge above the crease threshold:
  * relief          -- height of the step it represents (min far-corner distance)
  * setback         -- R*tan(phi/2), where the ball of radius R touches each face
  * extentA/extentB -- how far the smooth surface behind each face reaches,
                       measured perpendicular to the edge, staying on that
                       surface (walk stops at any feature edge)
  * seated          -- both extents >= setback, i.e. the ball touches both faces
                       inside their real extent rather than hanging off the end

Creases are grouped into chains (connected runs of steep convex edges) so the
chain length is available too.
"""
import sys, math, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from census import census, sub, dot, cross, norm

def run(path, R, thr=46.0, tol=1e-7):
    r = census(path, tol)
    verts, tris, nrm, adj = r['verts'], r['tris'], r['nrm'], r['adj']

    # feature edges (either sign) bound a smooth surface
    feat = set()
    conv = {}
    for key, ts in adj.items():
        if len(ts) != 2: feat.add(key); continue
        A, B = ts
        d = max(-1.0, min(1.0, dot(nrm[A], nrm[B])))
        phi = math.degrees(math.acos(d))
        aFar = next((k for k in tris[A] if k not in key), None)
        if aFar is None: continue
        concave = dot(nrm[B], sub(verts[aFar], verts[key[0]])) > 0
        if phi > thr:
            feat.add(key)
            if not concave: conv[key] = phi

    # smooth surfaces
    surf = [-1] * len(tris)
    sid = 0
    for i in range(len(tris)):
        if surf[i] >= 0: continue
        stack = [i]; surf[i] = sid
        while stack:
            t = stack.pop()
            for k in range(3):
                e = (min(tris[t][k], tris[t][(k+1)%3]), max(tris[t][k], tris[t][(k+1)%3]))
                if e in feat: continue
                for nb in adj[e]:
                    if surf[nb] < 0: surf[nb] = sid; stack.append(nb)
        sid += 1
    bysurf = collections.defaultdict(list)
    for i, s in enumerate(surf): bysurf[s].append(i)

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

    def extent(ti, key, budget):
        """Farthest the surface behind triangle ti reaches from the edge,
        walking only through non-feature edges, capped at budget.
        Also returns whether the contact point at `budget` is on the surface."""
        p0, p1 = verts[key[0]], verts[key[1]]
        e = norm(sub(p1, p0))
        n = nrm[ti]
        into = norm(cross(n, e))
        # orient `into` towards the triangle's far corner
        far = next(k for k in tris[ti] if k not in key)
        if dot(into, sub(verts[far], p0)) < 0: into = tuple(-x for x in into)
        mid = tuple((p0[i]+p1[i])/2 for i in range(3))
        target = tuple(mid[i] + budget*into[i] for i in range(3))
        best = 0.0; hit = math.inf
        stack = [ti]; seen = {ti}
        while stack:
            t = stack.pop()
            a, b, c = verts[tris[t][0]], verts[tris[t][1]], verts[tris[t][2]]
            for v in (a, b, c):
                best = max(best, dot(sub(v, mid), into))
            q = closest_on_tri(target, a, b, c)
            hit = min(hit, math.dist(q, target))
            for k in range(3):
                ek = (min(tris[t][k], tris[t][(k+1)%3]), max(tris[t][k], tris[t][(k+1)%3]))
                if ek in feat: continue
                for nb in adj[ek]:
                    if nb not in seen and surf[nb] == surf[ti]:
                        seen.add(nb); stack.append(nb)
        return best, hit

    rows = []
    for key, phi in conv.items():
        A, B = adj[key]
        setback = R * math.tan(math.radians(phi)/2.0)
        eA, hA = extent(A, key, setback)
        eB, hB = extent(B, key, setback)
        aFar = next(k for k in tris[A] if k not in key)
        bFar = next(k for k in tris[B] if k not in key)
        relief = min(abs(dot(nrm[B], sub(verts[aFar], verts[key[0]]))),
                     abs(dot(nrm[A], sub(verts[bFar], verts[key[0]]))))
        p0, p1 = verts[key[0]], verts[key[1]]
        mid = tuple((p0[i]+p1[i])/2 for i in range(3))
        rows.append(dict(key=key, phi=phi, setback=setback, eA=eA, eB=eB,
                         hA=hA, hB=hB, relief=relief, mid=mid,
                         length=math.dist(p0, p1),
                         seated=(eA >= setback - 1e-12 and eB >= setback - 1e-12),
                         contactOn=(hA < 1e-9 and hB < 1e-9)))
    return r, rows
