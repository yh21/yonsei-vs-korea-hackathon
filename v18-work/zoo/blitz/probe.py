import json,sys,subprocess,os
# usage: probe.py replay.json TURN TEAM(Y/K) [bot]  -> runs bot on the input of TURN (state after TURN-1)
path,T,team=sys.argv[1],int(sys.argv[2]),sys.argv[3]
botp=sys.argv[4] if len(sys.argv)>4 else './v18-work/zoo/blitz/bot'
d=json.load(open(path)); m=d['map']
opp='K' if team=='Y' else 'Y'
init=[f"INIT {m['width']} {m['height']}",f"TEAM {team}"]
for r in m['terrain']: init.append("MAP "+''.join(r))
init.append(f"BUILDINGS {len(m['buildings'])}")
for b in m['buildings']: init.append(f"{b['id']} {b['x']} {b['y']} {b['type']}")
init.append(f"BASE Y {m['bases']['Y'][0]} {m['bases']['Y'][1]}")
init.append(f"BASE K {m['bases']['K'][0]} {m['bases']['K'][1]}")
init.append("END")
st=d['turns'][T-2]['state'] if T>=2 else None
sc={b['id']:b['score'] for b in m['buildings']}
lines=[f"TURN {T}"]
if st is None:
    lines+=[f"RESOURCE 10 10","UNITS 0"]; own={b['id']:('N',0) for b in m['buildings']}; rev=[]
    units=[]
else:
    lines.append(f"RESOURCE {st['resources'][team]} {st['resources'][opp]}")
    order={'Y':0,'K':1}; ko={'F':0,'W':1,'S':2}
    units=sorted(st['units'],key=lambda u:(order[u[0]],ko[u[1]],u[3],u[2]))
    lines.append(f"UNITS {len(units)}")
    for tm,k,x,y,c in units: lines.append(f"{tm} {k} {x} {y} {c}")
    own={b['id']:(b['owner'],b['stage']) for b in st['buildings']}; rev=st['revealed'][team]
if st is None: lines.append(f"BUILDINGS {len(m['buildings'])}")
else: lines.append(f"BUILDINGS {len(m['buildings'])}")
for b in m['buildings']:
    o,s=own[b['id']]
    lines.append(f"{b['id']} {b['x']} {b['y']} {b['type']} {o} {s} {b['score'] if b['id'] in rev else -1}")
lines.append("END")
inp="\n".join(init)+"\n"+"\n".join(lines)+"\n"
env=dict(os.environ); env['BLITZ_DEBUG']='1'
r=subprocess.run([botp],input=inp,capture_output=True,text=True,env=env)
print(r.stdout); print(r.stderr[-4000:])
