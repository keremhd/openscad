"""Independent adversarial verification. Does NOT use meshutil for checks."""
import numpy as np
import trimesh
from collections import defaultdict, Counter
import spike

def my_edge_report(V, F):
    """Hand-rolled edge->face-count map. Independent of meshutil.
    Coordinate-dedup vertices myself at 1e-7."""
    V = np.asarray(V, float); F = np.asarray(F, int)
    key = np.round(V / 1e-7).astype(np.int64)
    uniq, inv = np.unique(key, axis=0, return_inverse=True)
    inv = inv.reshape(-1)
    Fd = inv[F]
    # drop degenerate
    deg = (Fd[:,0]==Fd[:,1])|(Fd[:,1]==Fd[:,2])|(Fd[:,0]==Fd[:,2])
    n_deg = int(deg.sum())
    Fd = Fd[~deg]
    # duplicate faces (as sorted tuples)
    facesig = Counter(tuple(sorted(t)) for t in Fd)
    n_dup = sum(c-1 for c in facesig.values() if c>1)
    # zero-area triangles in real coords
    Vd = uniq.astype(float)*1e-7
    zero_area = 0
    for a,b,c in Fd:
        ar = 0.5*np.linalg.norm(np.cross(Vd[b]-Vd[a], Vd[c]-Vd[a]))
        if ar < 1e-12: zero_area += 1
    und = defaultdict(list)
    for a,b,c in Fd:
        for u,v in ((a,b),(b,c),(c,a)):
            und[frozenset((int(u),int(v)))].append((int(u),int(v)))
    boundary=nonman=interior=orient=0
    for e,dirs in und.items():
        if len(dirs)==1: boundary+=1
        elif len(dirs)==2:
            interior+=1
            if dirs[0]==dirs[1]: orient+=1
        else: nonman+=1
    nV=len(uniq); nE=len(und); nF=len(Fd)
    return dict(nV=nV,nE=nE,nF=nF,euler=nV-nE+nF,boundary=boundary,
                nonmanifold=nonman,interior=interior,orient_mismatch=orient,
                n_degenerate=n_deg,n_dup_faces=n_dup,zero_area=zero_area)

print("="*60)
print("CLAIM 1: convex boss patch")
boss = spike.Boss(Rc=10.0,H=12.0,Rout=20.0,fn=24,t=2.0)
angles = boss.station_angles(i0=3,k=5)
V,F,info = spike.build_convex_seam(boss,angles,arc_seg=4)
r = my_edge_report(V,F)
print("my edge report:",r)
tm = trimesh.Trimesh(vertices=V,faces=F,process=False)
print("trimesh euler_number:",tm.euler_number,"is_watertight:",tm.is_watertight,
      "is_winding_consistent:",tm.is_winding_consistent)
print("incident_facets claimed:",info['incident_facets'],"full solid facets:",len(boss.full_solid().faces))

print("="*60)
print("CLAIM 2: groove bridge closed solid")
ys=[0.0,1.0,4.0,5.0]
Vg,Fg,ginfo=spike.build_groove_bridge(ys,y0=1.0,y1=4.0,depth=2.5,t=2.0,subdiv_limit=3)
rg=my_edge_report(Vg,Fg)
print("my edge report:",rg)
tmg=trimesh.Trimesh(vertices=Vg,faces=Fg,process=False)
print("trimesh euler_number:",tmg.euler_number,"is_watertight:",tmg.is_watertight,
      "is_winding_consistent:",tmg.is_winding_consistent,"volume:",tmg.volume)
print("ginfo:",ginfo)

# PROBE: does bridge actually change geometry? Compare subdiv_limit=0 vs =3 vs =6
print("--- probe: does 'bridge' matter? build with different subdiv_limit ---")
for lim in (0,1,3,6,10):
    Vb,Fb,gi=spike.build_groove_bridge(ys,y0=1.0,y1=4.0,depth=2.5,t=2.0,subdiv_limit=lim)
    rb=my_edge_report(Vb,Fb)
    print(f"  limit={lim}: seam_stations={gi['n_seam_stations']} subdiv={gi['subdiv_count']} "
          f"bridged={gi['n_bridged_spans']} closed_boundary={rb['boundary']} "
          f"watertight={trimesh.Trimesh(vertices=Vb,faces=Fb,process=False).is_watertight} "
          f"nV={rb['nV']} nF={rb['nF']}")

print("="*60)
print("CLAIM 4: ray vs clip -- probe with OFF-VERTEX angles")
# original angles are at polygon vertices. Test mid-facet angles.
dth = 2*np.pi/boss.fn
onvert = boss.station_angles(i0=3,k=5)
midfacet = onvert + dth/2   # aim between cylinder vertices
for label,angs in (("on-vertex",onvert),("mid-facet",midfacet)):
    d=spike.ray_contact_demo(boss,angs)
    print(f"  {label}: {d}")
# what radius does the tessellated wall actually sit at, at mid-facet?
print("  ideal Rc:",boss.Rc,"tessellated mid-facet radius:",boss.Rc*np.cos(dth/2))

print("="*60)
print("DETERMINISM: is the shuffle real & nontrivial?")
base=boss.station_angles(i0=3,k=5)
rng=np.random.default_rng(0)
for _ in range(3):
    sh=base.copy(); rng.shuffle(sh)
    print("  shuffled order:",np.round(sh,4),"== base order?",np.array_equal(sh,base))
