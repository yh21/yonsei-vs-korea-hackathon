import json,sys,glob
tot={}
n=0
for f in sorted(glob.glob(sys.argv[1])):
    d=json.load(open(f)); me='Y' if f.endswith('_Y.json') else 'K'; op='K' if me=='Y' else 'Y'
    ty={b['id']:b['type'] for b in d['map']['buildings']}
    n+=1
    for t in d['turns']:
        T=t['turn']
        if T in (15,25,35,50,80,120):
            h={me:0,op:0}; e={me:0,op:0}
            for b in t['state']['buildings']:
                if b['owner'] in h:
                    if ty[b['id']]=='HALL': h[b['owner']]+=1
                    if ty[b['id']]=='ENG': e[b['owner']]+=1
            k=(T,); v=tot.setdefault(T,[0,0,0,0]); v[0]+=h[me]; v[1]+=h[op]; v[2]+=e[me]; v[3]+=e[op]
for T in sorted(tot):
    v=tot[T]; print(f'T{T}: halls me {v[0]/n:.2f} opp {v[1]/n:.2f} | eng me {v[2]/n:.2f} opp {v[3]/n:.2f}  ({n} games)')
