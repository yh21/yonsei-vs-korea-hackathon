import json,sys
d=json.load(open(sys.argv[1])); lo=int(sys.argv[2]); hi=int(sys.argv[3])
ty={b['id']:(b['type'][:3],b['x'],b['y'],b['score']) for b in d['map']['buildings']}
print(d['teams']['Y']['name'],'(Y) vs',d['teams']['K']['name'],'(K)',d['result'])
prev={}
for t in d['turns']:
    n=t['turn']; s=t['state']
    cur={b['id']:b['owner'] for b in s['buildings']}
    ch=[f"{ty[i][0]}{ty[i][1]},{ty[i][2]}(s{ty[i][3]}):{prev.get(i,'-')}>{o}" for i,o in cur.items() if prev.get(i)!=o and n>1]
    prev=cur
    if lo<=n<=hi:
        u={'Y':[0,0,0],'K':[0,0,0]}
        for tm,k,x,y,c in s['units']: u[tm]['FWS'.index(k)]+=c
        print(n,'res',s['resources']['Y'],s['resources']['K'],'Y',u['Y'][:2],'K',u['K'][:2],' '.join(ch))
