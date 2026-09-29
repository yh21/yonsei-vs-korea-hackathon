import json,sys
d=json.load(open(sys.argv[1])); ty={b['id']:b['type'] for b in d['map']['buildings']}
sc={b['id']:b['score'] for b in d['map']['buildings']}
print(sys.argv[1].split('/')[-1], d['result']['reason'], d['result']['score'])
row=[]
for t in d['turns']:
    n=t['turn']
    if n%10 and n!=1: continue
    s=t['state']; h={'Y':0,'K':0}; e={'Y':0,'K':0}; p={'Y':0,'K':0}; w={'Y':0,'K':0}
    for b in s['buildings']:
        if b['owner'] in h:
            if ty[b['id']]=='HALL': h[b['owner']]+=1
            if ty[b['id']]=='ENG': e[b['owner']]+=1
            p[b['owner']]+=sc[b['id']]
    for tm,k,x,y,c in s['units']:
        if k=='W': w[tm]+=c
    row.append(f"{n}:H{h['Y']}{h['K']} E{e['Y']}{e['K']} p{p['Y']}-{p['K']} W{w['Y']}-{w['K']}")
print(' | '.join(row))
