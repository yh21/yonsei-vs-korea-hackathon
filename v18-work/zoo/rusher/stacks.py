import json,sys
d=json.load(open(sys.argv[1])); turns=[int(x) for x in sys.argv[2:]]
m=d['map']; bt={(b['x'],b['y']):b for b in m['buildings']}
H=len(m['terrain']); W=len(m['terrain'][0])
for t in d['turns']:
    if t['turn'] not in turns: continue
    s=t['state']; own={b['id']:b['owner'] for b in s['buildings']}
    cells={}
    for tm,k,x,y,c in s['units']: cells.setdefault((x,y),{}).setdefault(tm,[0,0,0])['FWS'.index(k)]+=c
    print('=== T',t['turn'],'res',s['resources'])
    g=[[('#' if m['terrain'][y][x]=='#' else '.') for x in range(W)] for y in range(H)]
    rows=[]
    for y in range(H):
        line=''
        for x in range(W):
            cell=cells.get((x,y)); b=bt.get((x,y))
            if b: ch=b['type'][0]+('y' if own[b['id']]=='Y' else 'k' if own[b['id']]=='K' else '_')
            else: ch=' '+g[y][x]
            u=''
            if cell:
                for tm in 'YK':
                    if tm in cell:
                        f,w,sc=cell[tm]; u+=(tm.lower() if tm=='Y' else 'K')+(str(w) if w else '')+('f' if f else '')
            line+=('%-2s'%ch)+'|'+('%-6s'%u)+' '
        rows.append(line)
    print('\n'.join(rows))
