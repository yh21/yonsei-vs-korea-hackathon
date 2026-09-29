import json,sys
d=json.load(open(sys.argv[1]))
turns=[int(a) for a in sys.argv[2:]]
m=d['map']; ter=m['terrain']
bl={ (b['x'],b['y']):b for b in m['buildings']}
tmap={'PLAZA':'P','HALL':'H','LIBRARY':'L','ENG':'E','HOSPITAL':'O','STATION':'T','WATCH':'W','DEPOT':'D'}
for t in d['turns']:
    if t['turn'] not in turns: continue
    s=t['state']; own={b['id']:b['owner'] for b in s['buildings']}
    print('--- turn',t['turn'],'res',s['resources'],'cmds Y:',t['commands']['Y'][:8],'K:',t['commands']['K'][:8])
    units={}
    for tm,k,x,y,c in s['units']:
        units.setdefault((x,y),[]).append(f"{tm}{k}{c}")
    for y in range(15):
        row=''
        for x in range(15):
            ch=ter[y][x]
            cell=''
            if (x,y) in bl:
                b=bl[(x,y)]; o=own[b['id']]
                cell=tmap[b['type']]+('y' if o=='Y' else ('k' if o=='K' else '.'))
            elif ch=='#': cell='##'
            elif ch=='H': cell='HB'
            else: cell='. '
            u=units.get((x,y))
            us=','.join(u) if u else ''
            row+=f"{cell}{us:<12}"
        print(row)
