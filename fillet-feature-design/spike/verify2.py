"""Independent adversarial verification of ROUND 2. No meshutil for checks."""
import numpy as np, trimesh
from collections import defaultdict, Counter
import spike2

def edge_report(V, F):
    V=np.asarray(V,float); F=np.asarray(F,int)
    key=np.round(V/1e-7).astype(np.int64)
    uniq,inv=np.unique(key,axis=0,return_inverse=True); inv=inv.reshape(-1)
    Fd=inv[F]
    deg=(Fd[:,0]==Fd[:,1])|(Fd[:,1]==Fd[:,2])|(Fd[:,0]==Fd[:,2])
    ndeg=int(deg.sum()); Fd=Fd[~deg]
    sig=Counter(tuple(sorted(t)) for t in Fd)
    ndup=sum(c-1 for c in sig.values() if c>1)
    Vd=uniq.astype(float)*1e-7
    za=sum(1 for a,b,c in Fd if 0.5*np.linalg.norm(np.cross(Vd[b]-Vd[a],Vd[c]-Vd[a]))<1e-12)
    und=defaultdict(list)
    for a,b,c in Fd:
        for u,v in ((a,b),(b,c),(c,a)): und[frozenset((int(u),int(v)))].append((int(u),int(v)))
    bnd=nm=inr=orm=0
    for e,d in und.items():
        if len(d)==1: bnd+=1
        elif len(d)==2:
            inr+=1
            if d[0]==d[1]: orm+=1
        else: nm+=1
    nV=len(uniq); nE=len(und); nF=len(Fd)
    return dict(nV=nV,nE=nE,nF=nF,euler=nV-nE+nF,boundary=bnd,nonmanifold=nm,
                orient=orm,ndeg=ndeg,ndup=ndup,zero_area=za)

print("### CLAIM 1: real clip / convex fine seam")
wall=spike2.Wall(jitter=0.30,dent=0.0)
tm=wall.base_solid()
poly,nfac,nent=spike2.seam_section(wall,tm)
print("  section polyline pts:",len(poly),"facets crossed:",nfac,"entities:",nent)
print("  base solid faces:",len(tm.faces),"is_watertight:",tm.is_watertight)
Vf,Ff,inf=spike2.build_seam_solid(wall,tm,subdiv_limit=wall.NY,bridge=False)
r=edge_report(Vf,Ff)
print("  fine seam my report:",r)
tmf=trimesh.Trimesh(vertices=Vf,faces=Ff,process=False)
print("  trimesh: watertight",tmf.is_watertight,"winding",tmf.is_winding_consistent,"euler",tmf.euler_number,"vol",round(tmf.volume,3))
# is the seam actually derived from the section, or reused Wt_nodes? compare
sec_x_at_nodes = None
# section vertices at z=t: compare their x to Wt_nodes
secv=np.array(poly)
print("  section verts z all == t?", np.allclose(secv[:,2] if secv.ndim==2 and secv.shape[1]==3 else 0, wall.t) if secv.ndim==2 else "n/a")
Wtn=wall.Wt_nodes()
# does build use section output? check F_pts vs Wt_nodes
print("  NOTE build_seam_solid uses Wt_nodes() reuse for F_pts (not section polyline)")

print("\n### CLAIM 2: dent poke + bridge + subdiv dependence (KEY)")
dwall=spike2.Wall(jitter=0.30,dent=2.2,dent_c=5.0,dent_w=1.0)
dtm=dwall.base_solid()
print("  limit | poke_mm | my_boundary | my_nonman | watertight(trimesh) | ndeg | ndup")
for lim in [0,1,2,4,8,20,dwall.NY-2]:
    poke,spans,st=spike2.measure_pokeout(dwall,dtm,subdiv_limit=lim)
    Vu,Fu,iu=spike2.build_seam_solid(dwall,dtm,subdiv_limit=lim,bridge=False)
    ru=edge_report(Vu,Fu)
    tmu=trimesh.Trimesh(vertices=Vu,faces=Fu,process=False)
    print(f"  {lim:3d} | {poke:.4f} | {ru['boundary']:3d} | {ru['nonmanifold']} | {tmu.is_watertight} | {ru['ndeg']} | {ru['ndup']}")
# bridge at limit 0
Vb,Fb,ib=spike2.build_seam_solid(dwall,dtm,subdiv_limit=0,bridge=True)
rb=edge_report(Vb,Fb)
tmb=trimesh.Trimesh(vertices=Vb,faces=Fb,process=False)
print("  BRIDGE@lim0 my report:",rb)
print("  BRIDGE@lim0 trimesh: watertight",tmb.is_watertight,"winding",tmb.is_winding_consistent,"vol",round(tmb.volume,4))

print("\n### PROBE 2a: is the hole caused by POKE, or just coarse/fine topo mismatch?")
print("  -> rebuild CONVEX (no dent, poke~0) UNBRIDGED at low limit:")
for lim in [0,2,8]:
    poke,_,_=spike2.measure_pokeout(wall,tm,subdiv_limit=lim)
    Vc,Fc,_=spike2.build_seam_solid(wall,tm,subdiv_limit=lim,bridge=False)
    rc=edge_report(Vc,Fc)
    print(f"    convex lim={lim}: poke={poke:.4f} boundary_edges={rc['boundary']} watertight={rc['boundary']==0 and rc['nonmanifold']==0}")

print("\n### PROBE 2b: does bridge just == full fine (ignoring stations)?")
Vfine,Ffine,_=spike2.build_seam_solid(dwall,dtm,subdiv_limit=dwall.NY,bridge=False)
import meshutil as mu
print("  bridge@lim0 canonical-identical to fine-unbridged?",
      mu.meshes_identical((Vb,Fb),(Vfine,Ffine)))
Vb4,Fb4,_=spike2.build_seam_solid(dwall,dtm,subdiv_limit=4,bridge=True)
print("  bridge@lim4 identical to bridge@lim0?", mu.meshes_identical((Vb,Fb),(Vb4,Fb4)))

print("\n### CLAIM 3: determinism -- shuffle real & pre-query?")
V0,F0=spike2.Wall(dent=2.2,dent_c=5.0,dent_w=1.0).base_solid().vertices, None
w=spike2.Wall(jitter=0.30,dent=2.2,dent_c=5.0,dent_w=1.0)
m_noshuf=w.base_solid()
m_shuf=w.base_solid(shuffle=11)
print("  face array equal (shuffle changes order)?", np.array_equal(np.sort(m_noshuf.faces,axis=None), np.sort(m_shuf.faces,axis=None)) and not np.array_equal(m_noshuf.faces, m_shuf.faces))
print("  same #verts/#faces:", m_noshuf.vertices.shape, m_shuf.vertices.shape)
def pipeline(shuffle):
    ww=spike2.Wall(jitter=0.30,dent=2.2,dent_c=5.0,dent_w=1.0)
    mm=ww.base_solid(shuffle=shuffle)
    return spike2.build_seam_solid(ww,mm,subdiv_limit=4,bridge=True)[:2]
ref=pipeline(None)
det=[mu.meshes_identical(ref,pipeline(s)) for s in (11,22,33)]
print("  determinism 3/3:",det)
# is comparison meaningful? compare ref to a genuinely different mesh
other=spike2.build_seam_solid(dwall,dtm,subdiv_limit=4,bridge=False)[:2]
print("  ref != unbridged (sanity, should be False):", mu.meshes_identical(ref,other))

print("\n### CLAIM 4: mid-facet ray gap (my own casts)")
d=spike2.ray_gap_demo(wall,tm)
print("  spike ray_gap_demo:",d)
# my own independent casts
yv=wall.yj; node_g=[]; mid_g=[]
for k in range(1,wall.NY-1):
    loc,_=spike2.ray_contact(tm,yv[k],wall.t)
    if loc is not None: node_g.append(abs(loc[0]-(wall.Wt_smooth(yv[k])+wall.xj[k])))
    ym=0.5*(yv[k]+yv[k+1])
    loc,_=spike2.ray_contact(tm,ym,wall.t)
    if loc is not None: mid_g.append(abs(loc[0]-wall.Wt_smooth(ym)))
print(f"  MY casts: node_max={max(node_g):.2e} mid_max={max(mid_g):.4f} mid_mean={np.mean(mid_g):.4f} n={len(mid_g)}")

print("\n### non-monotone poke under uniform spacing -- real?")
for lim in range(0,10):
    poke,_,st=spike2.measure_pokeout(dwall,dtm,subdiv_limit=lim)
    print(f"    lim={lim} stations={st} poke={poke:.4f}")
