import json,sys,glob
tot=0; missed=0; taken=0
for f in sorted(glob.glob(sys.argv[1])):
    d=json.load(open(f)); me='Y' if f.endswith('_Y.json') else 'K'; op='K' if me=='Y' else 'Y'
    W=d['map']['width']; H=d['map']['height']
    terr=d['map']['terrain']
    turns=d['turns']
    for i in range(len(turns)-1):
        T=turns[i]['turn']
        if T<20 or T>150: continue
        s=turns[i]['state']; n=turns[i+1]['state']
        my={};en={}
        for tm,k,x,y,c in s['units']:
            if k!='W': continue
            (my if tm==me else en)[(x,y)]=(my if tm==me else en).get((x,y),0)+c
        def N1(x,y):
            r=[(x,y)]
            for dx,dy in ((1,0),(-1,0),(0,1),(0,-1)):
                nx,ny=x+dx,y+dy
                if 0<=nx<W and 0<=ny<H and terr[ny][nx]!='#': r.append((nx,ny))
            return r
        for (ex,ey),m in en.items():
            if m<15: continue
            cells=N1(ex,ey)
            J=sum(my.get(c,0) for c in cells)
            thr=sum(en.get(c,0) for c in cells)
            adj=any(my.get(c,0)>0 for c in cells if c!=(ex,ey)) or my.get((ex,ey),0)>0
            if adj and J>thr+1:
                tot+=1
                # did my units end in e cell next turn?
                nmy={}
                for tm,k,x,y,c in n['units']:
                    if k=='W' and tm==me: nmy[(x,y)]=nmy.get((x,y),0)+c
                # opportunity taken if my W count at ex,ey next turn>0 or enemy stack gone
                nen={}
                for tm,k,x,y,c in n['units']:
                    if k=='W' and tm==op: nen[(x,y)]=nen.get((x,y),0)+c
                if nmy.get((ex,ey),0)>0 or nen.get((ex,ey),0)==0: taken+=1
                else: missed+=1
print('opportunities',tot,'taken(or dodged)',taken,'missed',missed)
