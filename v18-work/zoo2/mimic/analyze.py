import json,glob,re,sys,collections
files=sorted(glob.glob("../../replays/real_*.json"))
def cheb(a,b): return max(abs(a[0]-b[0]),abs(a[1]-b[1]))
def man(a,b): return abs(a[0]-b[0])+abs(a[1]-b[1])
rows=[]
for f in files:
    d=json.load(open(f)); n=int(re.findall(r'real_(\d+)',f)[0]); me=d['side']; en='Y' if me=='K' else 'K'
    T=d['turns']
    bl=T[0]['observation']['buildings']
    typ={b['id']:b['type'] for b in bl}; pos={b['id']:(b['x'],b['y']) for b in bl}
    # bases from map 'H'
    mp=T[0]['observation']['map']; H=[(x,y) for y in range(15) for x in range(15) if mp[y][x]=='H']
    ebase=min(H) if en=='Y' else max(H); mbase=max(H) if en=='Y' else min(H)
    # first ownership
    first={}; order=[]
    prevown={}
    for t in T:
        ob=t['observation']; tn=ob['turn']
        for b in ob['buildings']:
            if b['owner']==en and prevown.get(b['id'])!=en and ('e',b['id']) not in first:
                first[('e',b['id'])]=tn; order.append((tn,b['type'][:3],b['id']))
            prevown[b['id']]=b['owner']
    # per turn enemy unit stats
    units=[]
    for t in T:
        ob=t['observation']
        u=collections.defaultdict(int)
        for tm,k,x,y,c in ob['units']:
            if tm==en: u[(k,x,y)]+=c
        units.append(u)
    # spawn location estimate
    spawn_base=0; spawn_fwd=0; spawn_by_t=[]
    for i in range(1,len(T)):
        cur=units[i]; pre=units[i-1]
        sb=0; sf=0
        for (k,x,y),c in cur.items():
            if k=='S': continue
            near=sum(pc for (pk,px,py),pc in pre.items() if pk==k and abs(px-x)+abs(py-y)<=1)
            un=max(0,c-near)
            if (x,y)==ebase: sb+=un
            else: sf+=un
        # base cell units: cur at base count minus stay... base spawns appear at base or adjacent after moving; approx by total
        spawn_by_t.append((sb,sf))
    # W total by turn, F total, S total
    def tot(i,k): return sum(c for (kk,x,y),c in units[i].items() if kk==k)
    Wt=[tot(i,'W') for i in range(len(T))]; Ft=[tot(i,'F') for i in range(len(T))]; St=[tot(i,'S') for i in range(len(T))]
    # biggest W stack dist from my base / their base
    def big(i):
        if i>=len(units): return None
        best=None
        for (k,x,y),c in units[i].items():
            if k=='W' and (best is None or c>best[0]): best=(c,x,y)
        return best
    # first turn enemy W (>=5) within manhattan 5 of my base
    ft_near=None
    for i in range(len(T)):
        s=sum(c for (k,x,y),c in units[i].items() if k=='W' and man((x,y),mbase)<=5)
        if s>=5: ft_near=T[i]['observation']['turn']; break
    # enemy resource
    res=[t['observation']['resources'][en] for t in T]
    # station teleports suspect count: new W stack on Y station
    st=[pos[i] for i in typ if typ[i]=='STATION']
    rows.append(dict(n=n,turns=len(T)-1,reason=d['result']['reason'],ebase=ebase,mbase=mbase,order=order[:8],
        Wt=Wt,Ft=Ft,St=St,ft_near=ft_near,fwd=sum(x[1] for x in spawn_by_t),bas=sum(x[0] for x in spawn_by_t),res=res,
        big=[big(i) for i in (10,20,30,40,60)]))
json.dump(rows,open('an.json','w'))
for r in rows:
    W=r['Wt']; F=r['Ft']; S=r['St']
    g=lambda a,i:a[i] if i<len(a) else None
    print(f"g{r['n']:02d} end={r['turns']:3d}/{r['reason'][:4]} order={[(o[0],o[1]) for o in r['order'][:6]]}")
    print(f"     W@10/20/30/40/60={[g(W,i) for i in (10,20,30,40,60)]} Fmax20={max(F[:21])} F@40={g(F,40)} Smax={max(S)} near5W@{r['ft_near']} fwdspawn={r['fwd']} big={r['big'][:4]}")
