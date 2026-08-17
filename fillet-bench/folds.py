#!/usr/bin/env python3
"""Folds: face pairs the blend has laid back onto each other.

The one bench fault `mesh.py` cannot see. A fold is an edge whose two faces meet
at a dihedral of 180 degrees -- the surface doubles back along it and lies on top
of itself. That is a zero-volume flap: the solid stays closed, every edge is
still carried by exactly two faces, chi is unchanged and the genus is what it
was, so A1's whole instrument reads it as a correct solid. It shades as a bright
dart on the contact sheet and it is a defect.

    ./folds.py out.stl              one line per file: name and count
    ./folds.py out.stl --detail     the worst ten, with their coordinates
    ./folds.py --selftest           known answers only

Reads whatever `mesh.py` reads (ASCII STL and OFF, dispatched on the extension)
through mesh.py's own readers, so the two instruments never disagree about what
is in the file. Stdlib only, like mesh.py, so it runs wherever the build does.

WELD TOLERANCE IS ON EVERY LINE, for the same reason it is on mesh.py's: the
count moves with it. Vertices are quantised at --tol (default 1e-6) before any
edge is counted, exactly as mesh.py does it.

ANGLE. `--angle` is the dihedral above which a pair counts, in degrees, default
170. A fold proper is 180 and the ones this bench has found are 174-180; the
threshold is short of it because a fold between two tessellated surfaces (a
fillet strip lapping over its neighbour) closes to within a few degrees rather
than exactly. Below about 165 the count starts including honest creases on
coarse tessellations, so it is not a knob to turn down casually.

WHAT IS NOT COUNTED. Faces meeting at 180 degrees while wound the SAME way are
not a fold, they are a flat pair -- the test is on the angle between OUTWARD
normals, which is 180 only when the two point against each other. Degenerate
(zero-area) faces have no normal and are skipped rather than guessed at.
"""

import argparse
import collections
import math
import sys

from mesh import read_mesh


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _norm(a):
    return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])


def census(verts, faces, tol=1e-6, angle=170.0):
    """Return (count, rows). Each row is (dihedral, area, area, edge, tri, tri),
    the two triangles named by their welded vertex positions."""
    inv = 1.0 / tol
    key, pos, remap = {}, [], []
    for x, y, z in verts:
        k = (round(x * inv), round(y * inv), round(z * inv))
        if k not in key:
            key[k] = len(key)
            pos.append((x, y, z))
        remap.append(key[k])

    # Fan every polygon to triangles: a fold is between two TRIANGLES, and the
    # exporters write triangles anyway -- this only matters for an OFF file whose
    # faces are polygons.
    tris, nrm, area = [], [], []
    for face in faces:
        w = [remap[i] for i in face]
        for j in range(1, len(w) - 1):
            t = (w[0], w[j], w[j + 1])
            if len(set(t)) != 3:
                continue
            n = _cross(_sub(pos[t[1]], pos[t[0]]), _sub(pos[t[2]], pos[t[0]]))
            ln = _norm(n)
            tris.append(t)
            area.append(0.5 * ln)
            # A triangle smaller than the weld quantisation has no reliable normal
            # -- its direction is the rounding, not the surface -- so it is carried
            # for its edges and left out of every angle. Without this floor the
            # slivers a weld leaves behind read as folds: three of them do on this
            # bench, on models with no fold in them.
            nrm.append((n[0] / ln, n[1] / ln, n[2] / ln) if ln > tol * tol else None)

    carried = collections.defaultdict(list)
    for i, t in enumerate(tris):
        for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            carried[(min(a, b), max(a, b))].append(i)

    rows = []
    for e, ts in carried.items():
        if len(ts) != 2:
            continue
        f, g = ts
        if nrm[f] is None or nrm[g] is None:
            continue
        d = sum(nrm[f][k] * nrm[g][k] for k in range(3))
        ang = math.degrees(math.acos(max(-1.0, min(1.0, d))))
        if ang > angle:
            rows.append((ang, area[f], area[g], e, tris[f], tris[g]))
    rows.sort(key=lambda r: -max(r[1], r[2]))
    return len(rows), rows, pos


def selftest(tol=1e-6):
    """Known answers only, because a fold counter that fires on a correct solid is
    worse than none: a closed cube with no fold anywhere, a zero-volume flap whose
    every edge is one, and a pair leaning at 174 degrees that the default finds and
    a 175 degree threshold does not -- the band the bench's real folds sit in."""
    ok = True
    cube_v = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0),
              (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)]
    cube_f = [[0, 3, 2], [0, 2, 1], [4, 5, 6], [4, 6, 7], [0, 1, 5], [0, 5, 4],
              [1, 2, 6], [1, 6, 5], [2, 3, 7], [2, 7, 6], [3, 0, 4], [3, 4, 7]]
    n, _, _ = census(cube_v, cube_f, tol)
    print(f'cube                    folds={n} (want 0)  {"ok" if n == 0 else "FAIL"}')
    ok = ok and n == 0

    # A flap: two triangles on the same three points, wound against each other.
    # All three of its edges are folds, which is what a zero-volume flap is.
    flap_v = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    flap_f = [[0, 1, 2], [0, 2, 1]]
    n, _, _ = census(flap_v, flap_f, tol)
    print(f'doubled-back flap       folds={n} (want 3)  {"ok" if n == 3 else "FAIL"}')
    ok = ok and n == 3

    # A face laid back along one edge at 174 degrees: found at the default 170,
    # not found at 175. The bench's real folds sit in exactly this band.
    a = math.radians(180 - 174)
    lean_v = [(0, 0, 0), (0, 1, 0), (1, 0, 0), (math.cos(a), 0, math.sin(a))]
    lean_f = [[0, 1, 2], [1, 0, 3]]
    n1, _, _ = census(lean_v, lean_f, tol, 170.0)
    n2, _, _ = census(lean_v, lean_f, tol, 175.0)
    good = n1 == 1 and n2 == 0
    print(f'174 deg lean            170:{n1} 175:{n2} (want 1, 0)  {"ok" if good else "FAIL"}')
    ok = ok and good
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='*')
    ap.add_argument('--tol', type=float, default=1e-6, help='weld tolerance, mm (default 1e-6)')
    ap.add_argument('--angle', type=float, default=170.0,
                    help='dihedral above which a pair is a fold, deg (default 170)')
    ap.add_argument('--detail', action='store_true', help='print the worst ten with coordinates')
    ap.add_argument('--total', action='store_true', help='print the sum over all files as well')
    ap.add_argument('--selftest', action='store_true')
    args = ap.parse_args()

    if args.selftest:
        return 0 if selftest(args.tol) else 1
    if not args.files:
        ap.error('nothing to read')

    total = 0
    for path in args.files:
        try:
            verts, faces = read_mesh(path)
        except (OSError, ValueError) as exc:
            print(f'{path.split("/")[-1]}\t-\t{exc}')
            continue
        n, rows, pos = census(verts, faces, args.tol, args.angle)
        total += n
        print(f'{path.split("/")[-1]}\t{n}\ttol={args.tol:g} angle={args.angle:g}')
        if args.detail:
            for ang, af, ag, _e, tf, tg in rows[:10]:
                print(f'   ang={ang:.3f} areas={af:.5g},{ag:.5g}')
                for t in (tf, tg):
                    pts = ' '.join('(%.4f,%.4f,%.4f)' % pos[k] for k in t)
                    print(f'      {pts}')
    if args.total:
        print(f'TOTAL\t{total}\ttol={args.tol:g} angle={args.angle:g}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
