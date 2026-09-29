import json,sys,glob
files=sys.argv[1:]
import collections
acc=collections.defaultdict(list)
Fspawn=[];Wspawn=[]
for f in files:
    d=json.load(open(f))
    me='Y' if d['teams']['Y']['name']=='bot' else 'K'; op='K' if me=='Y' else 'Y'
    sc={b['id']:b['score'] for b in d['map']['buildings']}
    nf=0
    for t in d['turns']:
        n=t['turn']
        for c in t['commands'][me]:
            if c.startswith('SPAWN F'): nf+=int(c.split()[2])
        if n in (5,10,15,20,30,40,50,70,100,130,160):
            s=t['state']; p={'Y':0,'K':0}
            for b in s['buildings']:
                if b['owner'] in p: p[b['owner']]+=sc[b['id']]
            w={'Y':0,'K':0}
            fl={'Y':0,'K':0}
            for tm,k,x,y,c in s['units']:
                if k=='W': w[tm]+=c
                if k=='F': fl[tm]+=c
            acc[n].append((p[me]-p[op],w[me],w[op],fl[me],fl[op]))
    Fspawn.append(nf)
for n in sorted(acc):
    a=acc[n]; k=len(a)
    print(n,'ptdiff %.1f'%(sum(x[0] for x in a)/k),'W me %.0f op %.0f'%(sum(x[1] for x in a)/k,sum(x[2] for x in a)/k),'F me %.1f op %.1f'%(sum(x[3] for x in a)/k,sum(x[4] for x in a)/k))
print('avg F spawned',sum(Fspawn)/len(Fspawn))
