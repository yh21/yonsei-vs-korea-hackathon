import json,sys
d=json.load(open(sys.argv[1]))
turns=[int(x) for x in sys.argv[2:]]
m=d['map']; H=len(m['terrain']); Wd=len(m['terrain'][0])
bt={(b['x'],b['y']):b for b in m['buildings']}
for t in d['turns']:
    if t['turn'] not in turns: continue
    s=t['state']
    print('=== turn',t['turn'],'res',s['resources'])
    own={b['id']:b['owner'] for b in s['buildings']}
    grid=[[' . ' if m['terrain'][y][x]!='#' else ' # ' for x in range(Wd)] for y in range(H)]
    for (x,y),b in bt.items():
        o=own[b['id']]
        grid[y][x]=' '+b['type'][0].lower()+ ('y' if o=='Y' else 'k' if o=='K' else '_')
    for (x,y),b in bt.items(): pass
    for tm,k,x,y,c in s['units']: pass
    # unit summary
    cells={}
    for tm,k,x,y,c in s['units']:
        cells.setdefault((x,y),{}).setdefault(tm,[0,0,0])['FWS'.index(k)]+=c
    for y in range(H):
        row=''
        for x in range(Wd):
            u=cells.get((x,y))
            g=grid[y][x]
            if u:
                txt=''
                for tm in 'YK':
                    if tm in u:
                        f,w,sc=u[tm]
                        txt+=tm.lower() if False else ''
                g=g
            row+=g
        print(row)
    print('units:')
    for (x,y),u in sorted(cells.items(), key=lambda kv:(kv[0][1],kv[0][0])):
        print(' ',(x,y),{tm:u[tm] for tm in u}, bt[(x,y)]['type'] if (x,y) in bt else '')
