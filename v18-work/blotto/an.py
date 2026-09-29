import json,sys,re
d=json.load(open(sys.argv[1])); every=int(sys.argv[2]) if len(sys.argv)>2 else 10
sc={b['id']:b['score'] for b in d['map']['buildings']}
ty={b['id']:b['type'] for b in d['map']['buildings']}
cumsp={'Y':[0,0,0],'K':[0,0,0]}
print(d['teams']['Y']['name'],'(Y) vs',d['teams']['K']['name'],'(K)',d['result'])
for t in d['turns']:
    n=t['turn']
    for tm in 'YK':
        for c in t['commands'][tm]:
            m=re.match(r'SPAWN (\w) (\d+)',c)
            if m: cumsp[tm]['FWS'.index(m.group(1))]+=int(m.group(2))
    if n%every and n not in (1,2,3,5): continue
    s=t['state']; p={'Y':0,'K':0}; own={'Y':[],'K':[]}
    for b in s['buildings']:
        if b['owner'] in p: p[b['owner']]+=sc[b['id']]; own[b['owner']].append(ty[b['id']][:3])
    u={'Y':[0,0,0],'K':[0,0,0]}
    for tm,k,x,y,c in s['units']: u[tm]['FWS'.index(k)]+=c
    print(f"{n:3d} pts {p['Y']:2d}/{p['K']:2d} res {s['resources']['Y']:2d}/{s['resources']['K']:2d} FWS Y{u['Y']} K{u['K']} spawned Y{cumsp['Y']} K{cumsp['K']} | Y:{' '.join(own['Y'])} | K:{' '.join(own['K'])}")
