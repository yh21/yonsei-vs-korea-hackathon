"""replay json -> text dump for C++ sim test. usage: dump.py replay.json > out.txt"""
import json,sys
d=json.load(open(sys.argv[1]))
m=d['map']
out=[]
out.append(f"MAP {m['width']} {m['height']}")
for r in m['terrain']: out.append(r)
out.append(f"B {len(m['buildings'])}")
for b in m['buildings']: out.append(f"{b['id']} {b['x']} {b['y']} {b['type']} {b['score']}")
out.append(f"BASE Y {m['bases']['Y'][0]} {m['bases']['Y'][1]}")
out.append(f"BASE K {m['bases']['K'][0]} {m['bases']['K'][1]}")
out.append(f"T {len(d['turns'])}")
for t in d['turns']:
    for tm in 'YK':
        cmds=t['commands'][tm]
        out.append(f"C {tm} {len(cmds)}")
        out.extend(cmds)
    s=t['state']
    out.append(f"RES {s['resources']['Y']} {s['resources']['K']}")
    out.append(f"U {len(s['units'])}")
    for tm,k,x,y,c in s['units']: out.append(f"{tm} {k} {x} {y} {c}")
    out.append("O "+' '.join({'N':'0','Y':'1','K':'2'}[b['owner']] for b in s['buildings']))
print('\n'.join(out))
