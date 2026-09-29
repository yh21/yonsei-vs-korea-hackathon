import json,sys,glob,re
D={'U':(0,-1),'D':(0,1),'L':(-1,0),'R':(1,0)}
def moves(cmds):
    out=[]
    for c in cmds:
        m=re.match(r'MOVE (\d+) (\d+) (\w) (\d+) (\w)$',c)
        if m:
            x,y=int(m.group(1)),int(m.group(2)); dx,dy=D[m.group(5)]
            out.append(((x,y),(x+dx,y+dy),m.group(3),int(m.group(4))))
    return out
tot=0; rev=0; totF=0; revF=0
for f in sorted(glob.glob(sys.argv[1])):
    d=json.load(open(f)); me='Y' if f.endswith('_Y.json') else 'K'
    prev=None
    for t in d['turns']:
        mv=moves(t['commands'][me])
        if prev:
            # reversal: this turn moves units from B->A where previous turn moved A->B (same kind)
            pm={}
            for a,b,k,n in prev:
                pm[(b,a,k)]=pm.get((b,a,k),0)+n
            for a,b,k,n in mv:
                cnt=pm.get((a,b,k),0)
                r=min(cnt,n)
                if k=='W': tot+=n; rev+=r
                if k=='F': totF+=n; revF+=r
        prev=mv
print('W moves',tot,'reversal share',rev/max(1,tot),'| F moves',totF,'reversal share',revF/max(1,totF))
