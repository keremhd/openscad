#!/usr/bin/env python3
"""Per-run reader: comp/validity from mesh.py's rules plus a crease census that
ignores sliver triangles (the rounded-cube baseline carries 792 >11.25 deg
creases, all on triangles below 1e-4 area, so a floor is compulsory)."""
import math,collections,sys,json
def load(p):
    tris=[];cur=[]
    for line in open(p):
        s=line.split()
        if s and s[0]=='vertex': cur.append(tuple(float(x) for x in s[1:4]))
        elif s and s[0]=='endloop': tris.append(tuple(cur)); cur=[]
    return tris
def area(A,B,C):
    u=[B[i]-A[i] for i in range(3)];v=[C[i]-A[i] for i in range(3)]
    n=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
    return 0.5*math.sqrt(sum(c*c for c in n))
def go(path,tol,thr,floor,box):
    tris=load(path); q=lambda p: tuple(round(c/tol) for c in p)
    pos={};idx=[];ar=[]
    for t in tris:
        f=[]
        for p in t:
            k=q(p); pos.setdefault(k,p); f.append(k)
        if len(set(f))<3: ar.append(0.0); idx.append(f); continue
        idx.append(f); ar.append(area(*t))
    nrm=[]
    for f in idx:
        A,B,C=(pos[k] for k in f)
        u=[B[i]-A[i] for i in range(3)];v=[C[i]-A[i] for i in range(3)]
        n=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
        m=math.sqrt(sum(c*c for c in n)) or 1.0
        nrm.append([c/m for c in n])
    ed=collections.defaultdict(list)
    for i,f in enumerate(idx):
        for k in range(3): ed[tuple(sorted((f[k],f[(k+1)%3])))].append(i)
    nonman=sum(1 for e,t in ed.items() if len(t)>2); bnd=sum(1 for e,t in ed.items() if len(t)==1)
    # components by shared edge
    par=list(range(len(idx)))
    def find(x):
        # NOT `x = par[x] = par[par[x]]`: Python binds x first, so the write
        # lands on the new index and the structure is corrupted. That form read
        # comp=19 on a plain box. Caught by the control, per the standing rule.
        while par[x]!=x:
            par[x]=par[par[x]]; x=par[x]
        return x
    for e,ts in ed.items():
        for t in ts[1:]:
            a,b=find(ts[0]),find(t)
            if a!=b: par[a]=b
    comp=len({find(i) for i in range(len(idx))})
    v=len(pos); e=len(ed); f=len(idx)
    chi=v-e+f
    cre=[];cre_in=[]
    for k,ts in ed.items():
        if len(ts)!=2: continue
        if ar[ts[0]]<floor or ar[ts[1]]<floor: continue
        d=sum(nrm[ts[0]][i]*nrm[ts[1]][i] for i in range(3))
        a=math.degrees(math.acos(max(-1.0,min(1.0,d))))
        if a<=thr: continue
        mid=[(pos[k[0]][i]+pos[k[1]][i])/2 for i in range(3)]
        cre.append((a,mid))
        if box and all(box[2*i]<=mid[i]<=box[2*i+1] for i in range(3)): cre_in.append((a,mid))
    cre.sort(reverse=True); cre_in.sort(reverse=True)
    tiny=sum(1 for a in ar if 0<a<floor)
    return dict(v=v,e=e,f=f,comp=comp,bnd=bnd,nonman=nonman,chi=chi,
                valid=(bnd==0 and nonman==0 and chi%2==0),
                slivers=tiny,minarea=min(ar),
                creases=len(cre),worst=(round(cre[0][0],3) if cre else 0.0),
                worst_at=[round(c,4) for c in cre[0][1]] if cre else None,
                creases_box=len(cre_in),
                worst_box=(round(cre_in[0][0],3) if cre_in else 0.0),
                worst_box_at=[round(c,4) for c in cre_in[0][1]] if cre_in else None)
if __name__=='__main__':
    import argparse
    ap=argparse.ArgumentParser(); ap.add_argument('stl'); ap.add_argument('--tol',type=float,default=1e-6)
    ap.add_argument('--thr',type=float,default=11.25); ap.add_argument('--floor',type=float,default=1e-4)
    ap.add_argument('--box',nargs=6,type=float,default=None)
    a=ap.parse_args(); print(json.dumps(go(a.stl,a.tol,a.thr,a.floor,a.box)))
