import json,sys
# for each building lost by team ME (arg2): print my W within r=1,2,3,5 and enemy W within r=1,2
f=sys.argv[1]; me=sys.argv[2]; a=int(sys.argv[3]); b=int(sys.argv[4])
d=json.load(open(f)); bl={x['id']:x for x in d['map']['buildings']}
en='K' if me=='Y' else 'Y'
prev=None; prevs=None
for t in d['turns']:
    n=t['turn']; s=t['state']
    cur={x['id']:x['owner'] for x in s['buildings']}
    if prev and a<=n<=b:
        for i,o in cur.items():
            if prev[i]==me and o!=me:
                x,y=bl[i]['x'],bl[i]['y']
                # use previous state's units (before the turn's moves) for "could have defended"
                us=prevs['units']
                def cnt(team,r):
                    return sum(c for tm,k,ux,uy,c in us if tm==team and k=='W' and max(abs(ux-x),abs(uy-y))<=r)
                tot=sum(c for tm,k,ux,uy,c in us if tm==me and k=='W')
                print('T%d lost %s%d(%d,%d): myW r1=%d r2=%d r3=%d r5=%d tot=%d | enW r1=%d r2=%d'%(n,bl[i]['type'][:3],i,x,y,cnt(me,1),cnt(me,2),cnt(me,3),cnt(me,5),tot,cnt(en,1),cnt(en,2)))
    prev=cur; prevs=s
