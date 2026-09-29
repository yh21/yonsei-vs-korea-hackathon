import json,sys
d=json.load(open(sys.argv[1])); a=int(sys.argv[2]); b=int(sys.argv[3])
bl={x['id']:x for x in d['map']['buildings']}
prev=None
for t in d['turns']:
    n=t['turn']; s=t['state']
    cur={x['id']:x['owner'] for x in s['buildings']}
    if prev and a<=n<=b:
        for i,o in cur.items():
            if o!=prev[i]:
                x,y=bl[i]['x'],bl[i]['y']
                near=[]
                for tm,k,ux,uy,c in s['units']:
                    if abs(ux-x)<=2 and abs(uy-y)<=2: near.append('%s%s%d@%d,%d'%(tm,k,c,ux,uy))
                print('T%d %s%d(%d,%d) %s->%s'%(n,bl[i]['type'][:3],i,x,y,prev[i],o),' near:',' '.join(near))
    prev=cur
