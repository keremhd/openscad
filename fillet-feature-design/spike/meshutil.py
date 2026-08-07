"""
Mesh helpers for the B1 seam spike.

A "mesh" here is (V, F): V is an (n,3) float array of vertices, F is an (m,3)
int array of triangle vertex indices (CCW = outward normal).

The two things this spike must check for real -- MANIFOLDNESS and DETERMINISM --
live here, so they are plain, auditable, and independent of the geometry code.
"""
import numpy as np


# ---------------------------------------------------------------------------
# Canonicalization: fold a mesh to a unique, order-independent form so two
# runs (possibly with shuffled input) can be compared byte-for-byte.
# ---------------------------------------------------------------------------
def canonical(V, F, tol=1e-7):
    """Return (Vc, Fc) in a canonical form invariant to vertex/face ordering.

    - vertices are rounded to `tol`, deduplicated, sorted lexicographically
    - faces are remapped, each rotated so its smallest index comes first
      (preserving winding), then the face list is sorted lexicographically
    """
    V = np.asarray(V, float)
    F = np.asarray(F, int)

    # round + dedup vertices
    key = np.round(V / tol).astype(np.int64)
    # unique rounded keys -> new index, sorted lexicographically
    uniq, inv = np.unique(key, axis=0, return_inverse=True)
    inv = inv.reshape(-1)
    # representative coordinate = the rounded key * tol (stable, not input-order dependent)
    Vc = uniq.astype(float) * tol

    # remap faces to deduped indices
    Fr = inv[F]

    # drop degenerate faces (two identical corners after dedup)
    good = (Fr[:, 0] != Fr[:, 1]) & (Fr[:, 1] != Fr[:, 2]) & (Fr[:, 0] != Fr[:, 2])
    Fr = Fr[good]

    # rotate each face so smallest index is first, winding preserved
    out = np.empty_like(Fr)
    for i, (a, b, c) in enumerate(Fr):
        tri = [a, b, c]
        k = int(np.argmin(tri))
        out[i] = [tri[k], tri[(k + 1) % 3], tri[(k + 2) % 3]]

    # sort face list lexicographically
    order = np.lexsort((out[:, 2], out[:, 1], out[:, 0]))
    Fc = out[order]
    return Vc, Fc


def meshes_identical(m1, m2, tol=1e-7):
    V1, F1 = canonical(*m1, tol=tol)
    V2, F2 = canonical(*m2, tol=tol)
    if V1.shape != V2.shape or F1.shape != F2.shape:
        return False
    return np.array_equal(F1, F2) and np.allclose(V1, V2, atol=tol * 2)


# ---------------------------------------------------------------------------
# Manifold report: the real check.
# ---------------------------------------------------------------------------
def manifold_report(V, F, tol=1e-7):
    """Analyze the topology of (V,F) after coordinate-dedup.

    Returns a dict with:
      n_verts, n_faces, n_edges
      nonmanifold_edges : edges shared by >2 triangles (must be 0)
      boundary_edges    : edges shared by exactly 1 triangle (=holes/free rims)
      orientation_mismatches : shared edges not traversed in opposite directions
      euler             : V - E + F
      closed            : True iff boundary_edges==0 and nonmanifold==0
    """
    Vc, Fc = canonical(V, F, tol=tol)
    # undirected edge -> list of directed (u,v) as seen in faces
    from collections import defaultdict
    und = defaultdict(list)
    for a, b, c in Fc:
        for u, v in ((a, b), (b, c), (c, a)):
            und[frozenset((int(u), int(v)))].append((int(u), int(v)))

    nonmanifold = 0
    boundary = 0
    interior = 0
    orient_mismatch = 0
    for e, dirs in und.items():
        n = len(dirs)
        if n == 1:
            boundary += 1
        elif n == 2:
            interior += 1
            # consistent orientation: the two directed edges must be opposite
            if dirs[0] == dirs[1]:
                orient_mismatch += 1
        else:
            nonmanifold += 1

    n_edges = len(und)
    euler = len(Vc) - n_edges + len(Fc)
    return dict(
        n_verts=len(Vc),
        n_faces=len(Fc),
        n_edges=n_edges,
        nonmanifold_edges=nonmanifold,
        boundary_edges=boundary,
        interior_edges=interior,
        orientation_mismatches=orient_mismatch,
        euler=int(euler),
        closed=(boundary == 0 and nonmanifold == 0 and orient_mismatch == 0),
    )


def merge(*meshes):
    """Concatenate several (V,F) meshes into one (indices offset)."""
    Vs, Fs = [], []
    off = 0
    for V, F in meshes:
        V = np.asarray(V, float)
        F = np.asarray(F, int)
        Vs.append(V)
        Fs.append(F + off)
        off += len(V)
    return np.vstack(Vs), np.vstack(Fs) if Fs else np.zeros((0, 3), int)


def quad(a, b, c, d):
    """Two CCW triangles for a quad a-b-c-d (a,b,c,d are vertex indices)."""
    return [[a, b, c], [a, c, d]]
