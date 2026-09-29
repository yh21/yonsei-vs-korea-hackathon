import json,sys
# usage: board.py replay turn [me]   -- prints grid: each cell "b|mine|theirs" 
f=sys.argv[1]; n=int(sys.argv[2]); me=sys.argv[3] if len(sys.argv)>3 else 'Y'
d=json.load(open(f)); en='K' if me=='Y' else 'Y'
m=d['map']; T=[r if isinstance(r,str) else ''.join(r) for r in m['terrain']]
bl={(x['x'],x['y']):x for x in m['buildings']}
t=[x for x in d['turns'] if x['turn']==n][0]; s=t['state']
own={b['id']:b['owner'] for b in s['buildings']}
W={};F={}
for tm,k,x,y,c in s['units']:
    (W if k=='W' else F).setdefault((x,y),{}).setdefault(tm,0); (W if k=='W' else F)[(x,y)][tm]+=c
print('T',n,'res',s['resources'],'  cell: [B]mineW/theirW  (F: m=my f, e=enemy f)')
for y in range(15):
    row=''
    for x in range(15):
        ch=T[y][x]
        b=bl.get((x,y))
        tag='.'
        if ch=='#': tag='#####'
        else:
            bs=''
            if b:
                o=own[b['id']]
                bs=b['type'][0]+('m' if o==me else ('e' if o==en else 'n'))
            elif ch=='H': bs='HH'
            mw=W.get((x,y),{}).get(me,0); ew=W.get((x,y),{}).get(en,0)
            mf=F.get((x,y),{}).get(me,0); ef=F.get((x,y),{}).get(en,0)
            tag=(bs.ljust(2))+(('%d'%mw) if mw else '')+('/%d'%ew if ew else '')+('f' if mf else '')+('F' if ef else '')
        row+=tag.ljust(9)[:9]+' '
    print(row)
