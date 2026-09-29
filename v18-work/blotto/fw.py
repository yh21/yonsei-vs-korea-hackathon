import json,sys
d=json.load(open(sys.argv[1])); a=int(sys.argv[2]); b=int(sys.argv[3]); minw=int(sys.argv[4]) if len(sys.argv)>4 else 3
bl={x['id']:x for x in d['map']['buildings']}
pos={(x['x'],x['y']):x for x in bl.values()}
for t in d['turns']:
    n=t['turn']
    if n<a or n>b: continue
    s=t['state']; own={x['id']:x['owner'] for x in s['buildings']}
    line=[]
    for tm in 'YK':
        fs=[(x,y,c) for T,k,x,y,c in s['units'] if T==tm and k=='F']
        ws=[(x,y,c) for T,k,x,y,c in s['units'] if T==tm and k=='W' and c>=minw]
        def lab(x,y):
            b=pos.get((x,y)); return (b['type'][:3]+own[b['id']]) if b else ''
        line.append(f"{tm} F:"+' '.join(f"({x},{y}){lab(x,y)}" + (f"x{c}" if c>1 else '') for x,y,c in fs)+f" W>={minw}:"+' '.join(f"({x},{y})={c}{lab(x,y)}" for x,y,c in ws))
    print(n,'res',s['resources'],'\n   '+'\n   '.join(line)); print('   ev',[ (e['type'][:3],e['x'],e['y'],e.get('team','')) for e in t['events']])
