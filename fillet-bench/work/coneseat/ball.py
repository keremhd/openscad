"""Ball-clearance test: seat a ball of radius R at each steep convex edge from
the two adjacent face normals, then measure the true distance from its centre
to the whole mesh. A properly seated round ball touches at exactly R; a ball
with no room is buried."""
import sys, math, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from census import census, sub, dot, cross, norm
from seat import run as seatrun

def closest_on_tri(p,a,b,c):
    ab=sub(b,a); ac=sub(c,a); ap=sub(p,a)
    d1=dot(ab,ap); d2=dot(ac,ap)
    if d1<=0 and d2<=0: return a
    bp=sub(p,b); d3=dot(ab,bp); d4=dot(ac,bp)
    if d3>=0 and d4<=d3: return b
    vc=d1*d4-d3*d2
    if vc<=0 and d1>=0 and d3<=0:
        v=d1/(d1-d3) if d1!=d3 else 0.0
        return tuple(a[i]+v*ab[i] for i in range(3))
    cp=sub(p,c); d5=dot(ab,cp); d6=dot(ac,cp)
    if d6>=0 and d5<=d6: return c
    vb=d5*d2-d1*d6
    if vb<=0 and d2>=0 and d6<=0:
        w=d2/(d2-d6) if d2!=d6 else 0.0
        return tuple(a[i]+w*ac[i] for i in range(3))
    va=d3*d6-d5*d4
    if va<=0 and (d4-d3)>=0 and (d5-d6)>=0:
        w=(d4-d3)/((d4-d3)+(d5-d6))
        return tuple(b[i]+w*(c[i]-b[i]) for i in range(3))
    den=1.0/(va+vb+vc); v=vb*den; w=vc*den
    return tuple(a[i]+ab[i]*v+ac[i]*w for i in range(3))

def analyse(path, R, thr=46.0):
    r, rows = seatrun(path, R, thr)
    verts,tris,nrm,adj = r['verts'],r['tris'],r['nrm'],r['adj']
    cell = max(R, 0.5)
    grid=collections.defaultdict(list)
    for i,t in enumerate(tris):
        pts=[verts[v] for v in t]
        lo=[min(p[k] for p in pts) for k in range(3)]
        hi=[max(p[k] for p in pts) for k in range(3)]
        for ix in range(int(math.floor(lo[0]/cell)),int(math.floor(hi[0]/cell))+1):
         for iy in range(int(math.floor(lo[1]/cell)),int(math.floor(hi[1]/cell))+1):
          for iz in range(int(math.floor(lo[2]/cell)),int(math.floor(hi[2]/cell))+1):
            grid[(ix,iy,iz)].append(i)
    def near(p, rad):
        out=set()
        for ix in range(int(math.floor((p[0]-rad)/cell)),int(math.floor((p[0]+rad)/cell))+1):
         for iy in range(int(math.floor((p[1]-rad)/cell)),int(math.floor((p[1]+rad)/cell))+1):
          for iz in range(int(math.floor((p[2]-rad)/cell)),int(math.floor((p[2]+rad)/cell))+1):
            out.update(grid.get((ix,iy,iz),()))
        return out
    for e in rows:
        key=e['key']; A,B=adj[key]
        nA,nB=nrm[A],nrm[B]
        bis=norm(tuple(nA[i]+nB[i] for i in range(3)))
        cosphi=max(-1.0,min(1.0,dot(nA,nB))); phi=math.acos(cosphi)
        p0,p1=verts[key[0]],verts[key[1]]
        mid=tuple((p0[i]+p1[i])/2 for i in range(3))
        # convex: the round tool's ball sits INSIDE the material, tangent to both
        # faces from within (dir = -1 in FilletBuilder), so it is stepped along -bis.
        C=tuple(mid[i]- (R/math.cos(phi/2))*bis[i] for i in range(3))
        e['C']=C
        best=math.inf; bestAll=math.inf
        for ti in near(C, R*1.2):
            q0=closest_on_tri(C, verts[tris[ti][0]],verts[tris[ti][1]],verts[tris[ti][2]])
            bestAll=min(bestAll, math.dist(C,q0))
            if ti in (A,B): continue
            q=closest_on_tri(C, verts[tris[ti][0]],verts[tris[ti][1]],verts[tris[ti][2]])
            d=math.dist(C,q)
            if d<best: best=d
        e['clearOther']=best              # nearest mesh point that is NOT one of the two faces
        e['clearRel']=best/R
        e['clearAll']=bestAll/R
        # the two constructed contact feet, and how far they are OFF the mesh
        TA=tuple(C[i]+R*nA[i] for i in range(3))
        TB=tuple(C[i]+R*nB[i] for i in range(3))
        off=0.0
        for T in (TA,TB):
            bb=math.inf
            for ti in near(T, R*0.6):
                q0=closest_on_tri(T, verts[tris[ti][0]],verts[tris[ti][1]],verts[tris[ti][2]])
                bb=min(bb, math.dist(T,q0))
            off=max(off, bb)
        e['footOff']=off
        e['footOffRel']=off/R
    return r, rows

if __name__=='__main__':
    path,R = sys.argv[1], float(sys.argv[2])
    lab = sys.argv[3] if len(sys.argv)>3 else ''
    r,rows=analyse(path,R)
    corners=[(sx*4.24264,sy*4.24264,sz*4.24264) for sx in(1,-1) for sy in(1,-1) for sz in(1,-1)]
    for e in rows:
        e['dc']=min(math.dist(e['mid'],c) for c in corners)
    def q(xs):
        xs=sorted(xs); n=len(xs)
        return f'{xs[0]:.5g} / {xs[n//2]:.5g} / {xs[-1]:.5g}' if n else '-'
    print(f'== {lab} {path} R={R}: {len(rows)} steep convex edges')
    for name,sel in (('at a triple point (<3mm)',lambda x:x['dc']<3),('elsewhere',lambda x:x['dc']>=3)):
        s=[x for x in rows if sel(x)]
        print(f'  {name}: n={len(s)}')
        if not s: continue
        print(f'     dihedral      : {q([x["phi"] for x in s])}')
        print(f'     relief        : {q([x["relief"] for x in s])}      relief/R: {q([x["relief"]/R for x in s])}')
        print(f'     ball clearance to OTHER geometry / R : {q([x["clearRel"] for x in s])}')
        print(f'     ball dist to WHOLE mesh / R          : {q([x["clearAll"] for x in s])}')
        print(f'     seating deficit (1-clearAll)         : {q([1.0-x["clearAll"] for x in s])}')
