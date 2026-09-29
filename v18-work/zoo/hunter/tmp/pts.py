import json,sys
for f in sys.argv[1:]:
    d=json.load(open(f)); sc={b['id']:b['score'] for b in d['map']['buildings']}
    hunter='Y' if f.endswith('_Y.json') else 'K'
    out=[]
    for t in d['turns']:
        n=t['turn']
        if n in (5,10,15,20,25,30,40,60,80,100,120,160):
            p={'Y':0,'K':0}
            for b in t['state']['buildings']:
                if b['owner'] in p: p[b['owner']]+=sc[b['id']]
            u={'Y':0,'K':0}
            for tm,k,x,y,c in t['state']['units']:
                if k=='W': u[tm]+=c
            o='K' if hunter=='Y' else 'Y'
            out.append(f"{n}:{p[hunter]}-{p[o]}({u[hunter]}/{u[o]})")
    print(f.split('/')[-1][:24], ' '.join(out))
