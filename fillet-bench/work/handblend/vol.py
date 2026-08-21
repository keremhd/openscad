import sys,math
def vol(p):
    v=0.0;cur=[]
    for line in open(p):
        s=line.split()
        if s and s[0]=='vertex': cur.append([float(x) for x in s[1:4]])
        elif s and s[0]=='endloop':
            a,b,c=cur; cur=[]
            v+= (a[0]*(b[1]*c[2]-b[2]*c[1]) - a[1]*(b[0]*c[2]-b[2]*c[0]) + a[2]*(b[0]*c[1]-b[1]*c[0]))/6.0
    return abs(v)
for p in sys.argv[1:]: print(f"{p} vol={vol(p):.9f}")
