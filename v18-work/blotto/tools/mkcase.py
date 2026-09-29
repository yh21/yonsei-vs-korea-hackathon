import json,sys
d=json.load(open(sys.argv[1]))
out=[]
T=d['map']['terrain']
out.append("MAP")
out+=T
bl=d['map']['buildings']
pos={(b['x'],b['y']):b['id'] for b in bl}
for b in bl: out.append(f"B {b['id']} {b['x']} {b['y']} {b['type']} {b['score']}")
out.append("BASE Y %d %d"%tuple(d['map']['bases']['Y'])); out.append("BASE K %d %d"%tuple(d['map']['bases']['K']))
for t in d['turns']:
    out.append(f"TURN {t['turn']}")
    for tm in 'YK':
        for c in t['applied'][tm]:
            k=c['cmd']
            if k=='Spawn': out.append(f"C {tm} S {c['kind']} {c['count']} {-1 if c['x'] is None else c['x']} {-1 if c['y'] is None else c['y']}")
            elif k=='Move': out.append(f"C {tm} M {c['x']} {c['y']} {c['kind']} {c['count']} {c['direction']}")
            elif k=='Tele': out.append(f"C {tm} T {c['x']} {c['y']} {c['kind']} {c['count']} {c['tx']} {c['ty']}")
            elif k=='Priority':
                ids=[pos[(x,y)] for x,y in c['coords'] if (x,y) in pos]
                out.append(f"C {tm} P "+' '.join(map(str,ids)))
    s=t['state']
    out.append(f"E {s['resources']['Y']} {s['resources']['K']}")
    for tm,k,x,y,c in s['units']:
        if k in 'FW': out.append(f"U {tm} {k} {x} {y} {c}")
    for b in s['buildings']: out.append(f"O {b['id']} {b['owner']}")
open(sys.argv[2],'w').write('\n'.join(out)+'\n')
