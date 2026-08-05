import sys, math, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from census import dot
from step import planes_of
from ball import analyse
tgt, blend, R, lab = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4]
P = planes_of(tgt)
r, rows = analyse(blend, R)
def inplane(ti, mid):
    n=r['nrm'][ti]
    for (pn,pd) in P:
        if 1.0-dot(n,pn) <= 1e-4 and abs(dot(pn,mid)-pd) < 1e-9: return True
    return False
gen, art = [], []
for e in rows:
    a,b = r['adj'][e['key']]
    (gen if (inplane(a,e['mid']) and inplane(b,e['mid'])) else art).append(e)
def q(xs):
    xs=sorted(xs); n=len(xs)
    return f'{xs[0]:.5g} / {xs[n//2]:.5g} / {xs[-1]:.5g}' if n else '-'
print(f'== {lab}  R={R}   steep convex edges: {len(rows)}   genuine(on target planes)={len(gen)}  new(blend-made)={len(art)}')
for name, s in (('GENUINE', gen), ('BLEND-MADE', art)):
    if not s: print(f'   {name}: 0'); continue
    dfc=[1.0-x['clearAll'] for x in s]
    print(f'   {name}: n={len(s)}  dih {q([x["phi"] for x in s])}')
    print(f'      relief/R      min/med/max: {q([x["relief"]/R for x in s])}')
    print(f'      seatdeficit   min/med/max: {q(dfc)}   buried(>1e-6): {sum(1 for d in dfc if d>1e-6)}/{len(s)}')
    fo=[x['footOffRel'] for x in s]
    print(f'      footoff/R     min/med/max: {q(fo)}   foot off mesh(>1e-6): {sum(1 for d in fo if d>1e-6)}/{len(s)}')
    bad=[1 for x in s if (1.0-x['clearAll'])>1e-6 or x['footOffRel']>1e-6]
    print(f'      NOT SEATED (either test): {len(bad)}/{len(s)}')
print(f'   >>> relief/R: genuine min = {min((x["relief"]/R for x in gen), default=float("nan")):.5g} ; blend-made max = {max((x["relief"]/R for x in art), default=float("nan")):.5g}')
