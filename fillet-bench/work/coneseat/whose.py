"""Which rolling-ball position each face at a steep convex ridge belongs to.

A face of a concave (fillet) bead is the surface of a ball of radius R whose
centre sits one radius OUT along the outward normal. So for each of the two
faces at a ridge, c = centroid + R*n is that face's implied ball centre. If the
ridge is a step inside one blend surface the two centres coincide; if it is two
different tool surfaces crossing, they do not, and |cA - cB| is how far apart
the two tool placements are."""
import sys, math, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from census import census, sub, dot
from step import planes_of
tgt, blend, R, lab = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4]
P = planes_of(tgt)
r = census(blend, 1e-7)
verts, tris, nrm, adj = r['verts'], r['tris'], r['nrm'], r['adj']
def inplane(ti):
    n=nrm[ti]; p=verts[tris[ti][0]]
    return any(1.0-dot(n,pn)<=1e-9 and abs(dot(pn,p)-pd)<1e-9 for (pn,pd) in P)
def cen(ti):
    t=tris[ti]; c=tuple(sum(verts[v][k] for v in t)/3 for k in range(3))
    return tuple(c[k]+R*nrm[ti][k] for k in range(3)), c
corners=[(sx*6/math.sqrt(2),sy*6/math.sqrt(2),sz*6/math.sqrt(2)) for sx in(1,-1) for sy in(1,-1) for sz in(1,-1)]
rows=[]
for key,ts in adj.items():
    if len(ts)!=2: continue
    A,B=ts
    d=max(-1.0,min(1.0,dot(nrm[A],nrm[B]))); phi=math.degrees(math.acos(d))
    aFar=next((k for k in tris[A] if k not in key),None)
    if aFar is None or dot(nrm[B],sub(verts[aFar],verts[key[0]]))>0: continue   # concave
    if phi<=46.0: continue
    if inplane(A) or inplane(B): continue                                        # genuine target feature
    cA,pA=cen(A); cB,pB=cen(B)
    p0,p1=verts[key[0]],verts[key[1]]
    mid=tuple((p0[i]+p1[i])/2 for i in range(3))
    rows.append(dict(phi=phi, sep=math.dist(cA,cB), cA=cA, cB=cB, mid=mid,
                     dcornerA=min(math.dist(cA,c) for c in corners),
                     dcornerB=min(math.dist(cB,c) for c in corners),
                     axdistA=min(math.hypot(cA[i],cA[j]) for i,j in ((0,1),(1,2),(0,2))),
                     axdistB=min(math.hypot(cB[i],cB[j]) for i,j in ((0,1),(1,2),(0,2)))))
def q(xs):
    xs=sorted(xs); n=len(xs)
    return f'{xs[0]:.6g} / {xs[n//2]:.6g} / {xs[-1]:.6g}' if n else '-'
print(f'== {lab} R={R}: {len(rows)} blend-made steep convex ridges')
print(f'   |cA - cB|  (how far apart the two implied ball placements are), mm : {q([x["sep"] for x in rows])}')
print(f'   |cA - cB| / R                                                     : {q([x["sep"]/R for x in rows])}')
# a face belongs to the CORNER BALL if its implied centre sits at the single
# corner-ball centre; to a BEAD if the centre lies on a bead spine, which for
# cross is the curve at distance (6+?) ... use: spine points are equidistant
# from two cylinder axes.  Classify by how tightly the centres cluster.
allc=[x['cA'] for x in rows]+[x['cB'] for x in rows]
clus=[]
for c in allc:
    for k in clus:
        if math.dist(c,k[0])<1e-4: k[1]+=1; break
    else: clus.append([c,1])
clus.sort(key=lambda k:-k[1])
print(f'   distinct implied ball centres (1e-4 mm): {len(clus)} for {len(allc)} faces')
print(f'   largest clusters: {[(tuple(round(v,4) for v in k[0]),k[1]) for k in clus[:6]]}')
