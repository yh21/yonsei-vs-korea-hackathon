import json,sys
d=json.load(open(sys.argv[1]))
turns=[int(x) for x in sys.argv[2:]]
T=d['map']['terrain']
bl={ (b['x'],b['y']):b for b in d['map']['buildings']}
for t in d['turns']:
    if t['turn'] not in turns: continue
    s=t['state']
    print('=== turn',t['turn'],'res',s['resources'],'cmds',{k:v for k,v in t['commands'].items()} if 'commands' in t else '')
    own={b['id']:b['owner'] for b in s['buildings']}
    uu={}
    for tm,k,x,y,c in s['units']:
        uu.setdefault((x,y),[]).append(f"{tm}{k}{c}")
    for y in range(15):
        row=[]
        for x in range(15):
            ch=T[y][x] if isinstance(T[y],str) else T[y][x]
            cell=ch
            if (x,y) in bl:
                b=bl[(x,y)]; cell=b['type'][:2]+str(b['score'])+own[b['id']].lower() if own[b['id']]!='N' else b['type'][:2]+str(b['score'])+'.'
            u=uu.get((x,y))
            if u: cell=cell+'['+','.join(u)+']'
            row.append(cell.ljust(14))
        print(''.join(row))
