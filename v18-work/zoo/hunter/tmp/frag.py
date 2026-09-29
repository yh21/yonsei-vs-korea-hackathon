import json,sys,glob
def share(state,team):
    st={}
    for tm,k,x,y,c in state['units']:
        if tm==team and k=='W': st[(x,y)]=st.get((x,y),0)+c
    tot=sum(st.values())
    if tot==0: return None,0
    best=0
    for (x,y) in st:
        m=sum(c for (a,b),c in st.items() if abs(a-x)+abs(b-y)<=2)
        best=max(best,m)
    return best/tot,tot
rows=[]
for f in sorted(glob.glob(sys.argv[1])):
    d=json.load(open(f)); me='Y' if f.endswith('_Y.json') else 'K'; op='K' if me=='Y' else 'Y'
    sh_me=[];sh_op=[]
    for t in d['turns']:
        if 30<=t['turn']<=90 and t['turn']%3==0:
            a,ta=share(t['state'],me); b,tb=share(t['state'],op)
            if a is not None and b is not None: sh_me.append(a); sh_op.append(b)
    r=d['result']['score']; diff=r[me]-r[op]
    rows.append((diff,sum(sh_me)/len(sh_me),sum(sh_op)/len(sh_op)))
rows.sort()
for r in rows: print(f"{r[0]:+4d} main-share me {r[1]:.2f} op {r[2]:.2f}")
import statistics
w=[r for r in rows if r[0]>0]; l=[r for r in rows if r[0]<=0]
print('wins me',statistics.mean(r[1] for r in w) if w else None,'op',statistics.mean(r[2] for r in w) if w else None)
print('losses me',statistics.mean(r[1] for r in l),'op',statistics.mean(r[2] for r in l))
