import json,sys,re
d=json.load(open(sys.argv[1])); upto=int(sys.argv[2]) if len(sys.argv)>2 else 160
ty={b['id']:b['type'] for b in d['map']['buildings']}
pos={(b['x'],b['y']):b['id'] for b in d['map']['buildings']}
tot={t:dict(F=0,W=0,cap=0,neut=0,inc=0) for t in 'YK'}
prev=None
for t in d['turns']:
    n=t['turn']
    if n>upto: break
    for ev in t['events']:
        tm=ev.get('team')
        if ev['type']=='capture': tot[tm]['cap']+=1
        if ev['type']=='neutralize' and tm: tot[tm]['neut']+=1
    for tm in 'YK':
        for c in t['commands'][tm]:
            m=re.match(r'SPAWN (\w) (\d+)',c)
            if m: tot[tm][m.group(1)]+=int(m.group(2))
    if prev:
        for tm in 'YK':
            halls=sum(1 for b in prev['state']['buildings'] if b['owner']==tm and ty[b['id']]=='HALL')
            tot[tm]['inc']+=10+2*halls
    prev=t
for tm in 'YK': print(tm,tot[tm], 'res', prev['state']['resources'][tm])
