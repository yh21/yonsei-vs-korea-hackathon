import json,sys
d=json.load(open(sys.argv[1])); sc={b['id']:b['score'] for b in d['map']['buildings']}
ty={b['id']:b['type'] for b in d['map']['buildings']}
print(d['teams']['Y']['name'],'(Y) vs',d['teams']['K']['name'],'(K)',d['result'])
for t in d['turns']:
    n=t['turn']
    if n%10 and n not in (1,5): continue
    s=t['state']; p={'Y':0,'K':0}; own={'Y':[],'K':[]}
    for b in s['buildings']:
        if b['owner'] in p: p[b['owner']]+=sc[b['id']]; own[b['owner']].append(ty[b['id']][:3])
    u={'Y':[0,0,0],'K':[0,0,0]}
    for tm,k,x,y,c in s['units']: u[tm]['FWS'.index(k)]+=c
    print(n,'pts',p['Y'],p['K'],'res',s['resources']['Y'],s['resources']['K'],'FWS Y',u['Y'],'K',u['K'])
