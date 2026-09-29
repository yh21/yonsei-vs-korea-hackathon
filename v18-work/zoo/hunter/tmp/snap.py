import json,sys
d=json.load(open(sys.argv[1])); me=sys.argv[2]; lo=int(sys.argv[3]); hi=int(sys.argv[4])
op='K' if me=='Y' else 'Y'
bl={b['id']:b for b in d['map']['buildings']}
for t in d['turns']:
    n=t['turn']
    if not(lo<=n<=hi): continue
    s=t['state']
    def stacks(team,kind,minc=1):
        r=[(c,x,y) for tm,k,x,y,c in s['units'] if tm==team and k==kind and c>=minc]
        r.sort(reverse=True); return ' '.join(f'{c}@({x},{y})' for c,x,y in r)
    own={'Y':[],'K':[]}
    for b in s['buildings']:
        if b['owner'] in own:
            bb=bl[b['id']]; own[b['owner']].append(f"{bb['type'][:2]}({bb['x']},{bb['y']})")
    print(f"T{n} res {s['resources'][me]}/{s['resources'][op]}")
    print(f"  ME W: {stacks(me,'W',3)} | F: {stacks(me,'F')}")
    print(f"  EN W: {stacks(op,'W',3)} | F: {stacks(op,'F')}")
    print(f"  ME own: {' '.join(own[me])}")
    print(f"  EN own: {' '.join(own[op])}")
