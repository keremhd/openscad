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
    comp        connected components, by shared edge. Compared against --comp,
                the number of solids the model's SOURCE builds (default 1). A
                model that unions one solid and returns two has shed a fragment;
                shallow_crease renders two plates on purpose and says --comp 2.
    bnd         edges carried by exactly one face -- an open surface. NOTE that
                this is identically zero on anything OpenSCAD's Manifold backend
                exports: every edge of a Manifold is carried by two faces,
                welding only sums those counts, and a face welding collapses
                contributes an even count to the one edge it has left. Use it on
                meshes from elsewhere; it cannot fail on these. README says more.
    nonman      edges carried by more than two faces
    nmvert      PINCHED VERTICES: vertices whose incident faces do not form one
                edge-connected fan. Two fans at a point is a surface touching
                itself at that point -- not a solid, and invisible to every edge
                count, because each edge involved is still carried by exactly two
                faces. It announced itself only through chi parity before this
                existed, which is a coin flip on the pinch count: tee r=0.5 has
                TWO pinches and read chi=4 genus=1 VALID. The check does not care
                whether a fan is a closed cycle (an interior vertex of a closed
                surface) or an open one; it counts fans, not their shape.
    chi         Euler characteristic v - e + f
    genus       (2*comp - chi)/2, reported only when bnd, nonman AND nmvert are
                all 0 and chi is even. A pinched surface is not a closed
                orientable one, so its genus is a number with no referent.
    throat      alongside a nonzero genus: how narrow the handles are, as a
                PROXY -- the first weld tolerance on a decade ladder at which the
                genus stops being what it was. `throat<=0.01mm` reads "these
                handles do not survive welding at 0.01 mm". A hole a model is
                meant to have survives every weld tried and reports nothing. It
                is here so a 1.5 um handle can be told apart from a 0.4 mm loose
                sliver instead of the two weighing the same. Two narrower-looking
                definitions were tried first and both failed; throat_proxy() says
                how.
    parts       alongside comp > 1: the smallest component's face count and
                bounding box, which is what distinguishes a legitimate second
                solid from a 0.35 x 0.10 x 0.40 mm shard.

A mesh is VALID when bnd == 0, nonman == 0, nmvert == 0, chi is even, and comp
equals the expected component count.

    ./mesh.py --selftest            the synthetic controls, no files needed
"""

import argparse
import collections
import json
import math
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


class Find:
    """Union-find, small enough to keep here rather than depend on anything."""

    def __init__(self):
        self.p = {}

    def find(self, a):
        self.p.setdefault(a, a)
        while self.p[a] != a:
            self.p[a] = self.p[self.p[a]]
            a = self.p[a]
        return a

    def union(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self.p[ra] = rb

    def groups(self, items):
        g = collections.defaultdict(list)
        for it in items:
            g[self.find(it)].append(it)
        return list(g.values())


def analyse(verts, faces, tol, want_comp=1):
    """Weld at tol, then count edges by how many faces carry each, and fans by
    vertex. The vertex pass is the one that sees a surface pinched at a point;
    no edge count can, because every edge at a pinch still has exactly two
    faces."""
    inv = 1.0 / tol
    key = {}
    pos = []          # one representative position per welded vertex
    remap = []
    for x, y, z in verts:
        k = (round(x * inv), round(y * inv), round(z * inv))
        if k not in key:
            key[k] = len(key)
            pos.append((x, y, z))
        remap.append(key[k])

    edge_faces = collections.Counter()
    adj = collections.defaultdict(set)
    # For each welded vertex, the edges at it and which faces carry each. Two
    # faces at a vertex are in the same fan when an edge THROUGH THAT VERTEX
    # carries both. A closed surface's fan is a cycle and an open one's is a
    # path; neither is treated specially, because the count of fans is the
    # question and their shape is not.
    at_vert = collections.defaultdict(lambda: collections.defaultdict(list))
    vfaces = collections.defaultdict(set)
    kept = 0
    wfaces = []
    for face in faces:
        w = [remap[i] for i in face]
        # A face that collapses under welding is not a face.
        if len(set(w)) < 3:
            continue
        fi = kept
        kept += 1
        wfaces.append(w)
        n = len(w)
        for i in range(n):
            a, b = w[i], w[(i + 1) % n]
            if a == b:
                continue
            ek = (min(a, b), max(a, b))
            edge_faces[ek] += 1
            adj[ek].add(fi)
            at_vert[a][ek].append(fi)
            at_vert[b][ek].append(fi)
        for a in set(w):
            vfaces[a].add(fi)

    # Components over faces, joined by shared edges.
    fadj = collections.defaultdict(set)
    for e, fs in adj.items():
        fs = list(fs)
        for i in range(1, len(fs)):
            fadj[fs[0]].add(fs[i])
            fadj[fs[i]].add(fs[0])
    seen, comps = set(), []
    for start in range(kept):
        if start in seen:
            continue
        stack, members = [start], []
        seen.add(start)
        while stack:
            cur = stack.pop()
            members.append(cur)
            for nxt in fadj[cur]:
                if nxt not in seen:
                    seen.add(nxt)
                    stack.append(nxt)
        comps.append(members)
    comp = len(comps)

    # THE PINCH CHECK. Faces incident on a vertex, joined when an edge at that
    # vertex carries both. More than one group is more than one fan, which is
    # the surface touching itself at a point.
    pinches = []
    for a, edges in at_vert.items():
        inc = vfaces[a]
        if len(inc) < 2:
            continue
        uf = Find()
        for ek, fs in edges.items():
            for other in fs[1:]:
                uf.union(fs[0], other)
        fans = uf.groups(inc)
        if len(fans) > 1:
            pinches.append({'vert': a, 'pos': pos[a], 'fans': len(fans),
                            'faces': len(inc)})
    pinches.sort(key=lambda p: -p['fans'])

    v, e, f = len(key), len(edge_faces), kept
    bnd = sum(1 for c in edge_faces.values() if c == 1)
    nonman = sum(1 for c in edge_faces.values() if c > 2)
    nmvert = len(pinches)
    chi = v - e + f
    clean = not (bnd or nonman or nmvert or chi % 2)
    genus = (2 * comp - chi) // 2 if clean else None

    r = {
        'v': v, 'e': e, 'f': f, 'comp': comp, 'want_comp': want_comp,
        'bnd': bnd, 'nonman': nonman, 'nmvert': nmvert,
        'chi': chi, 'chi_odd': chi % 2 != 0,
        'genus': genus, 'tol': tol,
        'pinch_at': [p['pos'] for p in pinches[:4]],
        'valid': clean and comp == want_comp,
    }
    if comp > 1:
        r['parts'] = parts_report(comps, wfaces, pos)
    if genus:
        # The ladder must stop before the weld starts eating the tessellation
        # itself, so it is capped at the median edge length: below that a weld
        # closes throats, above it a weld closes facets and the reading is about
        # the mesh rather than about the solid.
        lens = sorted(math.dist(pos[a], pos[b]) for a, b in edge_faces)
        r['throat'] = throat_proxy(verts, faces, tol, genus, want_comp,
                                   lens[len(lens) // 2] if lens else None)
    return r


def parts_report(comps, wfaces, pos):
    """Per component: face count and bounding box, smallest first. A detached
    6-triangle 0.35 x 0.10 x 0.40 mm shard and a second plate the model meant to
    build are both comp=2; this is what tells them apart."""
    out = []
    for members in comps:
        vs = set()
        for fi in members:
            vs.update(wfaces[fi])
        xs = [pos[i][0] for i in vs]
        ys = [pos[i][1] for i in vs]
        zs = [pos[i][2] for i in vs]
        box = (max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))
        cx = sum(xs) / len(xs), sum(ys) / len(ys), sum(zs) / len(zs)
        out.append({'f': len(members), 'v': len(vs), 'box': box,
                    'centre': cx, 'diag': math.dist((0, 0, 0), box)})
    out.sort(key=lambda p: p['diag'])
    return out


def throat_proxy(verts, faces, tol, genus, want_comp, limit=None, steps=12):
    """How narrow the handles are, by WELDING THEM SHUT.

    A PROXY, and it says which one it is. The exact instrument is the shortest
    non-contractible cycle through the handle; this walks a ladder of weld
    tolerances instead and returns the first one at which the genus stops being
    what it was. A tunnel whose throat is t closes when vertices t apart are
    merged, so `throat <= X` reads "this handle does not survive welding at X".

    Two earlier versions of this function were wrong in opposite directions and
    both are worth not repeating. The closest pair of vertices sharing no face
    read 0.0015 mm on `hole_plate`, whose genus 1 is a hole a plate is meant to
    have -- it was measuring facet spacing across a fillet seam. Adding a
    six-hop separation test then read 0.84 mm on `cross` r=2.0, whose handles
    are 1.5 um: a handle narrower than a facet has its two sides one hop apart,
    so the hop test cannot see the very thing it was added for. Welding is the
    one test that scales with the handle rather than with the tessellation.

    Reads, on this bench: `cross` r=1.5 and r=2.0 keep genus 2 and 4 through a
    weld of 0.003 mm and lose it at 0.01, against a ball-removal throat of
    1.5 um -- micron-scale, boolean noise. `hole_plate` keeps genus 1 through
    0.3 mm, which is the hole it is named for. That is the separation the number
    exists to make. The ladder stops at the median edge length -- past that a
    weld is closing facets rather than throats, and a 4 mm torus hole in a coarse
    mesh reported 1.0 mm before that cap existed.
    """
    t = tol
    for _ in range(steps):
        t *= 10.0
        if limit is not None and t > limit:
            break
        r = analyse(verts, faces, t, want_comp)
        if r['genus'] != genus:
            return t
    return None      # survives every weld the tessellation allows: not narrow


def fmt(r):
    if 'error' in r:
        return f"{r['file']}: ERROR {r['error']}"
    g = 'n/a' if r['genus'] is None else r['genus']
    odd = ' CHI-ODD' if r['chi_odd'] else ''
    s = (f"{r['file']}: {'VALID' if r['valid'] else 'INVALID'}{odd} "
         f"v={r['v']} e={r['e']} f={r['f']} comp={r['comp']} "
         f"bnd={r['bnd']} nonman={r['nonman']} nmvert={r['nmvert']} "
         f"chi={r['chi']} genus={g} tol={r['tol']:g}")
    if r.get('throat') is not None:
        s += f" throat<={r['throat']:.3g}mm"
    if r['comp'] != r['want_comp']:
        s += f" (wanted comp={r['want_comp']})"
    if r['nmvert']:
        p = r['pinch_at'][0]
        s += f" pinch@({p[0]:.9g},{p[1]:.9g},{p[2]:.9g})"
        if r['nmvert'] > 1:
            s += f"+{r['nmvert'] - 1}more"
    if r.get('parts') and len(r['parts']) > 1:
        p = r['parts'][0]
        s += (f" smallest={p['f']}f {p['box'][0]:.3g}x{p['box'][1]:.3g}"
              f"x{p['box'][2]:.3g}mm at ({p['centre'][0]:.5g},"
              f"{p['centre'][1]:.5g},{p['centre'][2]:.5g})")
    return s


# --- synthetic controls ------------------------------------------------------
# The standing rule of this effort: run a new metric on a case whose answer is
# already known, BEFORE running it on the thing being measured. These five are
# built here rather than read from files so they cannot rot, and the fourth --
# two cubes meeting at exactly one vertex -- is the case the old criterion got
# wrong: nothing about it is a non-manifold edge.

def cube_at(ox, oy, oz, s=1.0):
    v = [(ox + s * x, oy + s * y, oz + s * z)
         for x, y, z in ((0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0),
                         (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1))]
    f = [[0, 3, 2, 1], [4, 5, 6, 7], [0, 1, 5, 4],
         [1, 2, 6, 5], [2, 3, 7, 6], [3, 0, 4, 7]]
    return v, f


def join(*meshes):
    verts, faces = [], []
    for v, f in meshes:
        n = len(verts)
        verts += v
        faces += [[i + n for i in face] for face in f]
    return verts, faces


def torus(nu=16, nv=8, R=3.0, r=1.0):
    verts, faces = [], []
    for i in range(nu):
        a = 2 * math.pi * i / nu
        for j in range(nv):
            b = 2 * math.pi * j / nv
            verts.append(((R + r * math.cos(b)) * math.cos(a),
                          (R + r * math.cos(b)) * math.sin(a),
                          r * math.sin(b)))
    idx = lambda i, j: (i % nu) * nv + (j % nv)
    for i in range(nu):
        for j in range(nv):
            faces.append([idx(i, j), idx(i + 1, j), idx(i + 1, j + 1), idx(i, j + 1)])
    return verts, faces


def selftest(tol=1e-6):
    """Every synthetic control, with the answer stated before the number."""
    cases = [
        # label, mesh, want_comp, expected fields
        ('cube', cube_at(0, 0, 0), 1,
         dict(valid=True, comp=1, nonman=0, nmvert=0, chi=2, genus=0)),
        ('torus', torus(), 1,
         dict(valid=True, comp=1, nonman=0, nmvert=0, chi=0, genus=1)),
        ('two disjoint cubes, comp declared 1', join(cube_at(0, 0, 0), cube_at(5, 0, 0)), 1,
         dict(valid=False, comp=2, nonman=0, nmvert=0, chi=4)),
        ('two disjoint cubes, comp declared 2', join(cube_at(0, 0, 0), cube_at(5, 0, 0)), 2,
         dict(valid=True, comp=2, nonman=0, nmvert=0, chi=4, genus=0)),
        ('two cubes at ONE SHARED VERTEX (the pinch)', join(cube_at(0, 0, 0), cube_at(1, 1, 1)), 1,
         dict(valid=False, nonman=0, nmvert=1, genus=None)),
        ('two cubes at ONE SHARED EDGE', join(cube_at(0, 0, 0), cube_at(1, 1, 0)), 1,
         dict(valid=False, nonman=1, nmvert=0, genus=None)),
        # THE tee r=0.5 SIGNATURE, built by hand: three cubes chained corner to
        # corner is two pinches, and two pinches is an EVEN chi. The old
        # criterion called this exact reading -- chi=4, comp=3, no non-manifold
        # edge -- VALID with genus 1. There is no tunnel in three cubes.
        ('three cubes chained at corners (two pinches, even chi)',
         join(cube_at(0, 0, 0), cube_at(1, 1, 1), cube_at(2, 2, 2)), 1,
         dict(valid=False, comp=3, nonman=0, nmvert=2, chi=4, chi_odd=False,
              genus=None)),
    ]
    fails = 0
    for label, (v, f), want_comp, want in cases:
        r = analyse(v, f, tol, want_comp)
        bad = {k: (want[k], r.get(k)) for k in want if r.get(k) != want[k]}
        r['file'] = label
        if bad:
            fails += 1
            print(f"  FAIL  {label}")
            print(f"        {fmt(r)}")
            for k, (w, got) in sorted(bad.items()):
                print(f"        {k}: wanted {w}, got {got}")
        else:
            print(f"  PASS  {fmt(r)}")
    # A pinch must be located, not merely counted: the shared corner of the two
    # cubes is (1,1,1) and nothing else may be reported.
    r = analyse(*join(cube_at(0, 0, 0), cube_at(1, 1, 1)), tol=tol)
    if r['pinch_at'] == [(1.0, 1.0, 1.0)]:
        print("  PASS  the pinch is located at (1,1,1), the shared corner")
    else:
        print(f"  FAIL  pinch located at {r['pinch_at']}, wanted [(1,1,1)]")
        fails += 1
    # And a cube's every vertex is ONE fan even though that fan is a closed
    # cycle rather than an open strip -- the check must not read a cycle as a
    # second fan, which would make every closed mesh pinched everywhere.
    r = analyse(*cube_at(0, 0, 0), tol=tol)
    if r['nmvert'] == 0:
        print("  PASS  a closed cube's cyclic vertex fans are one fan each")
    else:
        print(f"  FAIL  a plain cube reads {r['nmvert']} pinched vertices")
        fails += 1
    # THE THROAT PROXY, on two handles whose size is arithmetic. A torus of
    # R=3, r=1 has a 4 mm hole and must survive every weld tried; one of R=1,
    # r=0.9999 has a 0.0002 mm hole and must not survive 0.001. Both are genus 1
    # and identical in every other number, which is the entire point: no count
    # in this reader separates them and the throat does.
    wide = analyse(*torus(24, 12, 3.0, 1.0), tol=tol)
    if wide['genus'] == 1 and wide.get('throat') is None:
        print("  PASS  a 4 mm torus hole survives every weld tried and reports no throat")
    else:
        print(f"  FAIL  a 4 mm torus hole reported throat {wide.get('throat')} "
              f"(genus {wide['genus']})")
        fails += 1
    tight = analyse(*torus(24, 12, 1.0, 0.9999), tol=tol)
    if tight['genus'] == 1 and (tight.get('throat') or 1) <= 1e-3:
        print(f"  PASS  a 0.0002 mm torus hole reads throat<={tight['throat']:g} mm")
    else:
        print(f"  FAIL  a 0.0002 mm torus hole reported throat {tight.get('throat')} "
              f"(genus {tight['genus']})")
        fails += 1
    print("")
    if fails:
        print(f"MESH.PY SELFTEST FAILED: {fails} of the synthetic controls. "
              "Nothing measured with it counts.")
        return 1
    print(f"MESH.PY SELFTEST PASSED. {len(cases) + 4} controls, weld tolerance {tol:g}.")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='*')
    ap.add_argument('--comp', type=int, default=1,
                    help='component count the MODEL SOURCE builds (default 1); '
                         'anything else is a shed fragment')
    ap.add_argument('--selftest', action='store_true',
                    help='run the synthetic controls and exit')
    ap.add_argument('--tol', type=float, default=1e-6,
                    help='weld tolerance; printed with every result (default 1e-6)')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--compare', action='store_true',
                    help='read two files of the same solid (OFF and STL) and say '
                         'whether they agree; a disagreement is the exporter, not '
                         'the geometry, and must not stay silent')
    args = ap.parse_args()

    if args.selftest:
        return selftest(args.tol)
    if not args.files:
        ap.error('give a mesh file, or --selftest')

    out = []
    for path in args.files:
        try:
            r = analyse(*read_mesh(path), args.tol, args.comp)
        except Exception as exc:  # an unreadable mesh is a result, not a crash
            r = {'error': str(exc), 'valid': False}
        r['file'] = path
        out.append(r)

    if args.compare:
        if len(out) != 2:
            print('--compare wants exactly two files (the OFF and the STL)')
            return 2
        # Compare the VERDICT, not the face count. OFF writes the polygons it
        # has and STL writes triangles, so f and e differ on any model with a
        # quad in it -- a cube reads f=6 e=12 as OFF and f=12 e=18 as STL, and
        # both are chi=2 and valid. Flagging that as a disagreement buried the
        # real signal under a pile of cubes the first time this was run.
        keys = ('valid', 'comp', 'bnd', 'nonman', 'nmvert', 'chi', 'genus')
        a, b = out
        same = all(a.get(k) == b.get(k) for k in keys)
        for r in out:
            print(fmt(r))
        if same:
            note = ''
            if a.get('v') != b.get('v'):
                note = (f" -- but the vertex counts differ ({a['v']} vs {b['v']}): "
                        "OFF lost a distinction the weld kept")
            print(f'AGREE -- OFF and STL reach the same verdict{note} (tol={args.tol:g})')
        else:
            print('DISAGREE -- the OFF exporter changed the answer '
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
