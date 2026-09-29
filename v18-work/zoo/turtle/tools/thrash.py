import json,sys
DX={'U':(0,-1),'D':(0,1),'L':(-1,0),'R':(1,0)}
def moves(cmds):
    out={}
    for c in cmds:
        p=c.split()
        if p[0]=='MOVE' and p[3]=='W':
            x,y,n,dr=int(p[1]),int(p[2]),int(p[4]),p[5]
            dx,dy=DX[dr]; out[((x,y),(x+dx,y+dy))]=out.get(((x,y),(x+dx,y+dy)),0)+n
    return out
def run(f,team):
    d=json.load(open(f)); prev=None; tot=0; rev=0; per=[]
    for t in d['turns']:
        m=moves(t['commands'][team])
        if prev:
            for (s,dd),n in m.items():
                r=prev.get((dd,s),0)
                rev+=min(n,r)
            # units moving
        tot+=sum(m.values()); prev=m
    return tot,rev
for f in sys.argv[2:]:
    tot,rev=run(f,sys.argv[1])
    print(f.split('/')[-1],'W moves',tot,'reversal',rev,'%.1f%%'%(100*rev/max(1,tot)))
