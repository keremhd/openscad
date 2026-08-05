#!/usr/bin/env python3
"""Component and near-self-contact analysis for an ASCII STL. Stdlib only.

  probe.py comps FILE TOL...     per-component v/e/f/chi/genus, bbox, volume, centroid
  probe.py split  FILE TOL OUT   write each component to OUT_<i>.stl
  probe.py pinch  FILE TOL N     N vertex pairs that are near in space but far in the mesh
"""
import sys, math, collections


def read_stl(path):
    verts, faces, idx, cur = [], [], {}, []
    for line in open(path):
        w = line.split()
        if not w:
            continue
        if w[0] == 'vertex':
            k = (w[1], w[2], w[3])
            i = idx.get(k)
            if i is None:
                i = len(verts)
                idx[k] = i
                verts.append((float(w[1]), float(w[2]), float(w[3])))
            cur.append(i)
        elif w[0] == 'endloop':
            if len(cur) >= 3:
                faces.append(cur)
            cur = []
    return verts, faces


def weld(verts, faces, tol):
    q, remap, out = {}, [], []
    for v in verts:
        k = tuple(round(c / tol) for c in v)
        i = q.get(k)
        if i is None:
            i = len(out)
            q[k] = i
            out.append(v)
        remap.append(i)
    nf = []
    for f in faces:
        g = [remap[i] for i in f]
        h = [g[i] for i in range(len(g)) if g[i] != g[(i + 1) % len(g)]]
        if len(set(h)) >= 3:
            nf.append(h)
    return out, nf


def components(verts, faces):
    """Connected components of the face graph, joined by shared edges."""
    e2f = collections.defaultdict(list)
    for fi, f in enumerate(faces):
        for i in range(len(f)):
            e2f[frozenset((f[i], f[(i + 1) % len(f)]))].append(fi)
    seen, comps = [False] * len(faces), []
    for s in range(len(faces)):
        if seen[s]:
            continue
        stack, cur = [s], []
        seen[s] = True
        while stack:
            fi = stack.pop()
            cur.append(fi)
            f = faces[fi]
            for i in range(len(f)):
                for nb in e2f[frozenset((f[i], f[(i + 1) % len(f)]))]:
                    if not seen[nb]:
                        seen[nb] = True
                        stack.append(nb)
        comps.append(cur)
    return comps


def stats(verts, faces):
    vs = sorted({i for f in faces for i in f})
    es = {frozenset((f[i], f[(i + 1) % len(f)])) for f in faces for i in range(len(f))}
    v, e, f_ = len(vs), len(es), len(faces)
    chi = v - e + f_
    vol = 0.0
    cx = cy = cz = 0.0
    for f in faces:
        for k in range(1, len(f) - 1):
            a, b, c = verts[f[0]], verts[f[k]], verts[f[k + 1]]
            d = (a[0] * (b[1] * c[2] - b[2] * c[1])
                 - a[1] * (b[0] * c[2] - b[2] * c[0])
                 + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6.0
            vol += d
            cx += d * (a[0] + b[0] + c[0]) / 4.0
            cy += d * (a[1] + b[1] + c[1]) / 4.0
            cz += d * (a[2] + b[2] + c[2]) / 4.0
    bb = [[min(verts[i][k] for i in vs), max(verts[i][k] for i in vs)] for k in range(3)]
    cen = (cx / vol, cy / vol, cz / vol) if abs(vol) > 1e-15 else (0, 0, 0)
    return dict(v=v, e=e, f=f_, chi=chi, genus=(2 - chi) // 2, vol=vol, bbox=bb, centroid=cen)


def winding_inside(p, verts, faces):
    """Solid-angle winding number: >0.5 turn means p is inside the closed surface."""
    tot = 0.0
    for f in faces:
        for k in range(1, len(f) - 1):
            a = [verts[f[0]][i] - p[i] for i in range(3)]
            b = [verts[f[k]][i] - p[i] for i in range(3)]
            c = [verts[f[k + 1]][i] - p[i] for i in range(3)]
            la = math.sqrt(sum(x * x for x in a))
            lb = math.sqrt(sum(x * x for x in b))
            lc = math.sqrt(sum(x * x for x in c))
            det = (a[0] * (b[1] * c[2] - b[2] * c[1])
                   - a[1] * (b[0] * c[2] - b[2] * c[0])
                   + a[2] * (b[0] * c[1] - b[1] * c[0]))
            ab = sum(a[i] * b[i] for i in range(3))
            bc = sum(b[i] * c[i] for i in range(3))
            ca = sum(c[i] * a[i] for i in range(3))
            den = la * lb * lc + ab * lc + bc * la + ca * lb
            tot += 2.0 * math.atan2(det, den)
    return tot / (4 * math.pi)


def write_stl(path, verts, faces, name='part'):
    with open(path, 'w') as o:
        o.write('solid %s\n' % name)
        for f in faces:
            for k in range(1, len(f) - 1):
                a, b, c = verts[f[0]], verts[f[k]], verts[f[k + 1]]
                u = [b[i] - a[i] for i in range(3)]
                w = [c[i] - a[i] for i in range(3)]
                n = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]]
                ln = math.sqrt(sum(x * x for x in n)) or 1.0
                o.write('  facet normal %g %g %g\n    outer loop\n' % tuple(x / ln for x in n))
                for p in (a, b, c):
                    o.write('      vertex %.17g %.17g %.17g\n' % p)
                o.write('    endloop\n  endfacet\n')
        o.write('endsolid %s\n' % name)


def pinch(verts, faces, n):
    """Vertex pairs close in space but far along the surface -- necks and near-contacts."""
    adj = collections.defaultdict(set)
    for f in faces:
        for i in range(len(f)):
            a, b = f[i], f[(i + 1) % len(f)]
            adj[a].add(b)
            adj[b].add(a)
    vs = sorted(adj)
    # graph distance up to 4 rings
    near = {}
    for s in vs:
        seen = {s}
        frontier = {s}
        for _ in range(4):
            nxt = set()
            for x in frontier:
                nxt |= adj[x]
            nxt -= seen
            seen |= nxt
            frontier = nxt
        near[s] = seen
    out = []
    for ii, a in enumerate(vs):
        for b in vs[ii + 1:]:
            if b in near[a]:
                continue
            d = math.dist(verts[a], verts[b])
            out.append((d, a, b))
    out.sort()
    return out[:n]


def main():
    cmd, path = sys.argv[1], sys.argv[2]
    verts0, faces0 = read_stl(path)
    if cmd == 'comps':
        for t in sys.argv[3:]:
            tol = float(t)
            v, f = weld(verts0, faces0, tol)
            cs = components(v, f)
            print('%s tol=%g comp=%d' % (path, tol, len(cs)))
            for i, c in enumerate(sorted(cs, key=len, reverse=True)):
                s = stats(v, [f[j] for j in c])
                bb = s['bbox']
                print('  comp%d f=%d v=%d chi=%d genus=%d vol=%.6g mm^3 '
                      'bbox=[%.4f,%.4f]x[%.4f,%.4f]x[%.4f,%.4f] centroid=(%.4f,%.4f,%.4f)'
                      % (i, s['f'], s['v'], s['chi'], s['genus'], s['vol'],
                         bb[0][0], bb[0][1], bb[1][0], bb[1][1], bb[2][0], bb[2][1],
                         *s['centroid']))
    elif cmd == 'split':
        tol, out = float(sys.argv[3]), sys.argv[4]
        v, f = weld(verts0, faces0, tol)
        cs = sorted(components(v, f), key=len, reverse=True)
        for i, c in enumerate(cs):
            write_stl('%s_%d.stl' % (out, i), v, [f[j] for j in c], 'comp%d' % i)
            print('wrote %s_%d.stl (%d faces)' % (out, i, len(c)))
        if len(cs) > 1:
            small = [f[j] for j in cs[1]]
            big = [f[j] for j in cs[0]]
            cen = stats(v, small)['centroid']
            w = winding_inside(cen, v, big)
            print('small-component centroid (%.4f,%.4f,%.4f) winding wrt main = %.4f -> %s'
                  % (*cen, w, 'INSIDE main (void)' if abs(w) > 0.5 else 'OUTSIDE main (stray shard)'))
    elif cmd == 'pinch':
        tol, n = float(sys.argv[3]), int(sys.argv[4])
        v, f = weld(verts0, faces0, tol)
        for d, a, b in pinch(v, f, n):
            print('%.6f  (%.3f,%.3f,%.3f) -- (%.3f,%.3f,%.3f)' % (d, *v[a], *v[b]))


main()
