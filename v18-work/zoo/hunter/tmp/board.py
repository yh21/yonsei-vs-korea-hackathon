import json,sys
d=json.load(open(sys.argv[1])); turns=[int(x) for x in sys.argv[2:]]
m=d['map']; W=m['width']; H=m['height']
for t in d['turns']:
    if t['turn'] not in turns: continue
    s=t['state']
    grid=[[ '.' if c!='#' else '#' for c in row] for row in m['terrain']]
    cell={}
    for b in m['buildings']:
        own={o['id']:o['owner'] for o in s['buildings']}[b['id']]
        cell[(b['x'],b['y'])]=[b['type'][:2]+str(b['score'])+own.lower()]
    for tm,k,x,y,c in s['units']:
        cell.setdefault((x,y),[]).append(f"{tm}{k}{c}")
    print("TURN",t['turn'],'res',s['resources'])
    for y in range(H):
        row=[]
        for x in range(W):
            if (x,y) in cell: row.append('/'.join(cell[(x,y)]))
            elif grid[y][x]=='#': row.append('##')
            else: row.append('.')
        print(' '.join(f"{r:>14}" if False else r for r in row))
    print(t['commands'])
