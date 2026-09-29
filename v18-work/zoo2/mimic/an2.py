import json,glob,re,collections,statistics as st
files=sorted(glob.glob("../../replays/real_*.json"))
agg=collections.defaultdict(list)
for f in files:
    d=json.load(open(f)); n=int(re.findall(r'real_(\d+)',f)[0]); me=d['side']; en='Y'
    T=d['turns']; bl=T[0]['observation']['buildings']
    typ={(b['x'],b['y']):b['type'] for b in bl}
    mp=T[0]['observation']['map']; H=[(x,y) for y in range(15) for x in range(15) if mp[y][x]=='H']; eb=min(H)
    hos=0;sta=0;other=0; hosT=[]
    prev=None
    for t in T:
        ob=t['observation']; cur=collections.defaultdict(int)
        for tm,k,x,y,c in ob['units']:
            if tm==en: cur[(k,x,y)]+=c
        if prev is not None:
            for (k,x,y),c in cur.items():
                if k=='S': continue
                near=sum(pc for (pk,px,py),pc in prev.items() if pk==k and abs(px-x)+abs(py-y)<=1)
                un=max(0,c-near)
                if (x,y)==eb or un==0: continue
                ty=typ.get((x,y))
                if ty=='HOSPITAL': hos+=un; hosT.append(ob['turn'])
                elif ty=='STATION': sta+=un
                else: other+=un
        prev=cur
    # enemy tele-like: count turns where a station-appearance <=5
    agg['hos'].append(hos);agg['sta'].append(sta);agg['oth'].append(other)
    print(f"g{n:02d} hospital-appear W/F={hos} station-appear={sta} other={other} firstHosSpawnT={hosT[:1]}")
