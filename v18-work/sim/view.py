import json,sys
d=json.load(open(sys.argv[1])); turns=[int(x) for x in sys.argv[2:]]
bl={b['id']:b for b in d['map']['buildings']}
T=d['map']['terrain']
ab={'PLAZA':'P','HALL':'H','LIBRARY':'L','ENG':'E','HOSPITAL':'+','STATION':'S','WATCH':'V','DEPOT':'D'}
for t in d['turns']:
    if t['turn'] not in turns: continue
    s=t['state']; g=[[ '#' if T[y][x]=='#' else '.' for x in range(15)] for y in range(15)]
    cell={}
    for tm,k,x,y,c in s['units']:
        cell.setdefault((x,y),{}).setdefault(tm,{})[k]=c
    print('turn',t['turn'],'res',s['resources'],'cmdY',t['commands']['Y'][:6],'cmdK',t['commands']['K'][:6])
    own={b['id']:b['owner'] for b in s['buildings']}
    for y in range(15):
        row=''
        for x in range(15):
            ch=T[y][x]
            txt=''
            bb=[b for b in bl.values() if b['x']==x and b['y']==y]
            if bb:
                b=bb[0]; o=own[b['id']]; txt=ab[b['type']]+('y' if o=='Y' else 'k' if o=='K' else '_')+str(b['score'])
            elif ch=='H': txt='HOM'
            elif ch=='#': txt='###'
            else: txt='...'
            u=cell.get((x,y),{})
            us=''
            for tm in 'YK':
                if tm in u:
                    us+=tm.lower()+str(u[tm].get('W',0))+('f' if u[tm].get('F') else '')
            row+=f'{txt:>4}{us:<7}'
        print(row)
