#!/usr/bin/env python3
"""Connected components, per-component volume and extents of an STL.

The question this exists to answer is "did the tool shed a crumb", which a
picture answers badly and a bounding box answers wrongly. A bead's cross-section
is r across however wide the brush is, so a bbox cannot tell a 1 mm bead from a
6 mm one -- only volume can. And a figure that cuts a model open leaves bead ends
floating if its cutting box is not generous, which looks exactly like a tool
shedding crumbs and is not, so the component count has to be read next to the
per-component volumes rather than on its own.

    ./meshstat.py a.stl b.stl          one line per file
    ./meshstat.py --pieces a.stl       one line per connected component

Stdlib only, so it runs wherever the build does.
"""

import collections
import struct
import sys

# Vertices within this are treated as the same point when joining triangles into
# components. Manifold emits exact duplicates, so this only has to absorb the
# ascii round-trip.
QUANT = 1e-7


def read_stl(path):
    """Return a list of (v0, v1, v2) triangles, from binary or ascii STL."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:5] == b'solid' and b'facet' in data[:2000]:
        tris, cur, tok, i = [], [], data.decode('utf8', 'replace').split(), 0
        while i < len(tok):
            if tok[i] == 'vertex':
                cur.append(tuple(float(tok[i + j]) for j in (1, 2, 3)))
                if len(cur) == 3:
                    tris.append(tuple(cur))
                    cur = []
                i += 4
            else:
                i += 1
        return tris
    count = struct.unpack('<I', data[80:84])[0]
    tris, off = [], 84
    for _ in range(count):
        v = struct.unpack('<12fH', data[off:off + 50])
        tris.append((v[3:6], v[6:9], v[9:12]))
        off += 50
    return tris


def _key(v):
    return tuple(round(c / QUANT) for c in v)


def _tet_volume(t):
    """Signed volume of the tetrahedron on the origin and this triangle."""
    a, b, c = t
    return (a[0] * (b[1] * c[2] - b[2] * c[1])
            - a[1] * (b[0] * c[2] - b[2] * c[0])
            + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6.0


def components(tris):
    """Group triangles into connected components, joined through shared vertices.

    Returns a list of dicts, largest volume first, each with the component's
    triangles, its signed volume and its axis-aligned extents.
    """
    parent = {}

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def join(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb

    for t in tris:
        ks = [_key(v) for v in t]
        for k in ks:
            parent.setdefault(k, k)
        join(ks[0], ks[1])
        join(ks[1], ks[2])

    grouped = collections.defaultdict(list)
    for t in tris:
        grouped[find(_key(t[0]))].append(t)

    out = []
    for ts in grouped.values():
        xs = [v[0] for t in ts for v in t]
        ys = [v[1] for t in ts for v in t]
        zs = [v[2] for t in ts for v in t]
        out.append({
            'tris': ts,
            'volume': abs(sum(_tet_volume(t) for t in ts)),
            'extent': ((min(xs), max(xs)), (min(ys), max(ys)), (min(zs), max(zs))),
        })
    out.sort(key=lambda c: -c['volume'])
    return out


def analyse(path):
    tris = read_stl(path)
    comps = components(tris)
    return {
        'tris': len(tris),
        'components': len(comps),
        'volume': sum(_tet_volume(t) for t in tris),
        'pieces': comps,
    }


def main(argv):
    per_piece = '--pieces' in argv
    paths = [a for a in argv if not a.startswith('--')]
    if not paths:
        print(__doc__)
        return 1
    for path in paths:
        try:
            r = analyse(path)
        except Exception as exc:                     # noqa: BLE001 - report and continue
            print(f'{path}: ERROR {exc}')
            continue
        name = path.split('/')[-1]
        vols = ', '.join(f'{p["volume"]:.4g}' for p in r['pieces'][:8])
        more = f' (+{len(r["pieces"]) - 8} more)' if len(r['pieces']) > 8 else ''
        print(f'{name:28s} tris={r["tris"]:7d} components={r["components"]:3d} '
              f'volume={r["volume"]:12.4f}  per piece: {vols}{more}')
        if per_piece:
            for p in r['pieces']:
                (x0, x1), (y0, y1), (z0, z1) = p['extent']
                print(f'{"":6s}{p["volume"]:10.4f} mm3   '
                      f'x {x0:7.2f}..{x1:7.2f}  y {y0:7.2f}..{y1:7.2f}  z {z0:7.2f}..{z1:7.2f}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
