import json,sys,re
from collections import defaultdict
path=sys.argv[1]; team=sys.argv[2]
d=json.load(open(path))
dirs={'U':(0,-1),'D':(0,1),'L':(-1,0),'R':(1,0)}
base=d['map']['bases'][team]
bt={(b['x'],b['y']):b for b in d['map']['buildings']}
prev=defaultdict(int) # (x,y,kind)->count for team
prevE=defaultdict(int)
turns=d['turns']
res=[]
for i,t in enumerate(turns):
    cmds=t['commands'][team]
    cur=defaultdict(int); 
    pos=dict(prev)
    # spawn
    arr=defaultdict(int)
    for c in cmds:
        p=c.split()
        if p[0]=='SPAWN':
            k=p[1]; n=int(p[2]); 
            x,y=(int(p[3]),int(p[4])) if len(p)==5 else tuple(base)
            pos[(x,y,k)]=pos.get((x,y,k),0)+n
    for c in cmds:
        p=c.split()
        if p[0]=='MOVE':
            x,y,k,n,dr=int(p[1]),int(p[2]),p[3],int(p[4]),p[5]
            n=min(n,pos.get((x,y,k),0))
            pos[(x,y,k)]-=n
            arr[(x+dirs[dr][0],y+dirs[dr][1],k)]+=n
    exp=defaultdict(int)
    for k,v in pos.items():
        if v>0: exp[k]+=v
    for k,v in arr.items(): exp[k]+=v
    act=defaultdict(int)
    en=defaultdict(int)
    for tm,k,x,y,c in t['state']['units']:
        if tm==team: act[(x,y,k)]+=c
        else: en[(x,y,k)]+=c
    for k,v in exp.items():
        if k[2]=='F' and act.get(k,0)<v:
            x,y=k[0],k[1]
            res.append((t['turn'],(x,y),v-act.get(k,0),bt[(x,y)]['type'] if (x,y) in bt else '', 'enW_prev='+str(sum(prevE[(xx,yy,'W')] for xx in range(x-1,x+2) for yy in range(y-1,y+2) if abs(xx-x)+abs(yy-y)<=1)), 'myW_here_exp='+str(exp.get((x,y,'W'),0))))
    prev={k:v for k,v in act.items()}
    prevE=en
print(len(res))
for r in res[:80]: print(r)
