import json,sys,subprocess,time
# usage: timing.py replay.json TEAM bot  -> feed each recorded state to bot, measure latency per turn
path,team,botp=sys.argv[1],sys.argv[2],sys.argv[3]
d=json.load(open(path)); m=d['map']; opp='K' if team=='Y' else 'Y'
init=[f"INIT {m['width']} {m['height']}",f"TEAM {team}"]
for r in m['terrain']: init.append("MAP "+''.join(r))
init.append(f"BUILDINGS {len(m['buildings'])}")
for b in m['buildings']: init.append(f"{b['id']} {b['x']} {b['y']} {b['type']}")
init.append(f"BASE Y {m['bases']['Y'][0]} {m['bases']['Y'][1]}"); init.append(f"BASE K {m['bases']['K'][0]} {m['bases']['K'][1]}"); init.append("END")
p=subprocess.Popen([botp],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1)
p.stdin.write("\n".join(init)+"\n"); p.stdin.flush()
order={'Y':0,'K':1}; ko={'F':0,'W':1,'S':2}
mx=0;tot=0;n=0;lat=[]
for i,t in enumerate(d['turns']):
    T=i+1
    if i==0:
        res=(10,10); units=[]; own={b['id']:('N',0) for b in m['buildings']}; rev=[]
    else:
        st=d['turns'][i-1]['state']
        res=(st['resources'][team],st['resources'][opp]); units=sorted(st['units'],key=lambda u:(order[u[0]],ko[u[1]],u[3],u[2]))
        own={b['id']:(b['owner'],b['stage']) for b in st['buildings']}; rev=st['revealed'][team]
    lines=[f"TURN {T}",f"RESOURCE {res[0]} {res[1]}",f"UNITS {len(units)}"]
    for tm,k,x,y,c in units: lines.append(f"{tm} {k} {x} {y} {c}")
    lines.append(f"BUILDINGS {len(m['buildings'])}")
    for b in m['buildings']:
        o,s=own[b['id']]; lines.append(f"{b['id']} {b['x']} {b['y']} {b['type']} {o} {s} {b['score'] if b['id'] in rev else -1}")
    lines.append("END")
    t0=time.time(); p.stdin.write("\n".join(lines)+"\n"); p.stdin.flush()
    while True:
        l=p.stdout.readline()
        if l.strip()=="END": break
        if l=="": raise SystemExit("bot died")
    dt=(time.time()-t0)*1000; lat.append((dt,T)); mx=max(mx,dt); tot+=dt; n+=1
p.stdin.close(); p.wait()
lat.sort(reverse=True); print("top",[(round(a,1),b) for a,b in lat[:4]]); print(f"turns={n} max={mx:.1f}ms avg={tot/n:.1f}ms")
