#!/usr/bin/env python3
"""Topological validity of an OFF mesh: the A1 and A3 instrument.

Reads what OpenSCAD writes with `-o out.off` and answers the only question the
acceptance gate asks of a solid -- is it closed, orientable and of the genus it
should be. Stdlib only, so it runs wherever the build does.

    ./mesh.py out.off               one line
    ./mesh.py --json out.off        machine-readable

WELD TOLERANCE IS NOT A DETAIL. Counts of edges carried by more than two faces
move with it -- the same mesh has read 205 at 1e-5 and 0 at 1e-6 in this effort.
Vertices are quantised at --tol, which defaults to 1e-6, and the tolerance is
printed on every line so no number is ever quoted without it.

Reported per mesh:

    v e f       vertices, edges, faces after welding
    comp        connected components, by shared edge
    bnd         edges carried by exactly one face -- an open surface. NOTE that
                this is identically zero on anything OpenSCAD's Manifold backend
                exports: every edge of a Manifold is carried by two faces,
                welding only sums those counts, and a face welding collapses
                contributes an even count to the one edge it has left. Use it on
                meshes from elsewhere; it cannot fail on these. README says more.
    nonman      edges carried by more than two faces
    chi         Euler characteristic v - e + f
    genus       (2*comp - chi)/2, meaningful only when bnd and nonman are 0.
                An odd chi is reported as such: no closed orientable surface has
                one, so it is a proof of invalidity rather than a measurement.

A mesh is VALID when bnd == 0, nonman == 0, and chi is even.
"""

import argparse
import collections
import json
import sys


def read_stl(path):
    """Return (verts, faces) from an ASCII STL.

    STL is the exact format of the two: export_stl.cc prints through
    double_conversion::ToShortest, which round-trips a double exactly, while
    export_off.cc streams with default ostream precision -- six significant
    figures. So an OFF file has already lost any distinction finer than about
    1e-5 at a coordinate of 7, before --tol is ever consulted.

    Whether that loss changes an answer is a question to measure, not to assume,
    and --compare is here to measure it. Measured on rib_into_boss at $fn=32 it
    changes nothing: the two files agree exactly. See README.

    STL carries no vertex indices -- it repeats coordinates per facet -- so
    identical coordinate triples are shared here. That is exact-value sharing,
    not welding; welding still happens in analyse() at --tol, the same as for
    OFF, so the two formats are compared through the same reader.
    """
    verts, faces, idx, cur = [], [], {}, []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line.startswith('vertex'):
                t = tuple(float(x) for x in line.split()[1:4])
                if t not in idx:
                    idx[t] = len(verts)
                    verts.append(t)
                cur.append(idx[t])
            elif line.startswith('endloop'):
                if len(cur) >= 3:
                    faces.append(cur)
                cur = []
    if not faces:
        raise ValueError(f'{path}: no facets -- not an ASCII STL?')
    return verts, faces


def read_mesh(path):
    """Dispatch on extension. Binary STL is not handled and says so."""
    if path.endswith('.stl'):
        with open(path, 'rb') as f:
            head = f.read(6)
        if not head.lower().startswith(b'solid'):
            raise ValueError(f'{path}: binary STL, not ASCII -- export with -o *.stl')
        return read_stl(path)
    return read_off(path)


def read_off(path):
    """Return (verts, faces) from an ASCII OFF file."""
    with open(path) as f:
        tok = []
        for line in f:
            line = line.split('#')[0].strip()
            if line:
                tok.append(line)
    if not tok or not tok[0].startswith('OFF'):
        raise ValueError(f'{path}: not an OFF file')
    # "OFF" may carry the counts on its own line or share the first one.
    head = tok[0][3:].split()
    idx = 0
    if not head:
        head = tok[1].split()
        idx = 2
    else:
        idx = 1
    nv, nf = int(head[0]), int(head[1])
    verts = []
    for i in range(nv):
        p = tok[idx + i].split()
        verts.append((float(p[0]), float(p[1]), float(p[2])))
    faces = []
    for i in range(nf):
        p = [int(x) for x in tok[idx + nv + i].split()]
        faces.append(p[1:1 + p[0]])
    return verts, faces


def analyse(verts, faces, tol):
    """Weld at tol, then count edges by how many faces carry each."""
    inv = 1.0 / tol
    key = {}
    remap = []
    for x, y, z in verts:
        k = (round(x * inv), round(y * inv), round(z * inv))
        if k not in key:
            key[k] = len(key)
        remap.append(key[k])

    edge_faces = collections.Counter()
    adj = collections.defaultdict(set)
    kept = 0
    for fi, face in enumerate(faces):
        w = [remap[i] for i in face]
        # A face that collapses under welding is not a face.
        if len(set(w)) < 3:
            continue
        kept += 1
        for i in range(len(w)):
            a, b = w[i], w[(i + 1) % len(w)]
            if a == b:
                continue
            edge_faces[(min(a, b), max(a, b))] += 1
            adj[(min(a, b), max(a, b))].add(kept - 1)

    # Components over faces, joined by shared edges.
    fadj = collections.defaultdict(set)
    for e, fs in adj.items():
        fs = list(fs)
        for i in range(1, len(fs)):
            fadj[fs[0]].add(fs[i])
            fadj[fs[i]].add(fs[0])
    seen, comp = set(), 0
    for start in range(kept):
        if start in seen:
            continue
        comp += 1
        stack = [start]
        seen.add(start)
        while stack:
            cur = stack.pop()
            for nxt in fadj[cur]:
                if nxt not in seen:
                    seen.add(nxt)
                    stack.append(nxt)

    v, e, f = len(key), len(edge_faces), kept
    bnd = sum(1 for c in edge_faces.values() if c == 1)
    nonman = sum(1 for c in edge_faces.values() if c > 2)
    chi = v - e + f
    return {
        'v': v, 'e': e, 'f': f, 'comp': comp, 'bnd': bnd, 'nonman': nonman,
        'chi': chi, 'chi_odd': chi % 2 != 0,
        'genus': None if (bnd or nonman or chi % 2) else (2 * comp - chi) // 2,
        'tol': tol,
        'valid': bnd == 0 and nonman == 0 and chi % 2 == 0,
    }


def fmt(r):
    if 'error' in r:
        return f"{r['file']}: ERROR {r['error']}"
    g = 'n/a' if r['genus'] is None else r['genus']
    odd = ' CHI-ODD' if r['chi_odd'] else ''
    return (f"{r['file']}: {'VALID' if r['valid'] else 'INVALID'}{odd} "
            f"v={r['v']} e={r['e']} f={r['f']} comp={r['comp']} "
            f"bnd={r['bnd']} nonman={r['nonman']} chi={r['chi']} "
            f"genus={g} tol={r['tol']:g}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='+')
    ap.add_argument('--tol', type=float, default=1e-6,
                    help='weld tolerance; printed with every result (default 1e-6)')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--compare', action='store_true',
                    help='read two files of the same solid (OFF and STL) and say '
                         'whether they agree; a disagreement is the exporter, not '
                         'the geometry, and must not stay silent')
    args = ap.parse_args()

    out = []
    for path in args.files:
        try:
            r = analyse(*read_mesh(path), args.tol)
        except Exception as exc:  # an unreadable mesh is a result, not a crash
            r = {'error': str(exc), 'valid': False}
        r['file'] = path
        out.append(r)

    if args.compare:
        if len(out) != 2:
            print('--compare wants exactly two files (the OFF and the STL)')
            return 2
        keys = ('valid', 'v', 'e', 'f', 'comp', 'bnd', 'nonman', 'chi', 'genus')
        a, b = out
        same = all(a.get(k) == b.get(k) for k in keys)
        for r in out:
            print(fmt(r))
        print('AGREE' if same else 'DISAGREE', '-- OFF and STL read the same solid'
              if same else '-- the OFF exporter changed the answer',
              f'(tol={args.tol:g})')
        return 0 if same else 3

    if args.json:
        print(json.dumps(out, indent=2))
    else:
        for r in out:
            print(fmt(r))
    return 0 if all(r.get('valid') for r in out) else 1


if __name__ == '__main__':
    sys.exit(main())
