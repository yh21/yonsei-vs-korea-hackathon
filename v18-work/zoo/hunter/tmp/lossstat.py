import json,sys,glob,collections
rows=[]
for f in sorted(glob.glob(sys.argv[1])):
    d=json.load(open(f)); me='Y' if f.endswith('_Y.json') else 'K'; op='K' if me=='Y' else 'Y'
    bl={b['id']:b for b in d['map']['buildings']}
    prev=None
    for t in d['turns']:
        s=t['state']
        if prev and 25<=t['turn']<=60:
            po={b['id']:b['owner'] for b in prev['buildings']}
            for b in s['buildings']:
                if po[b['id']]==me and b['owner']!=me:
                    bb=bl[b['id']]
                    def W(state,team,r):
                        return sum(c for tm,k,x,y,c in state['units'] if tm==team and k=='W' and max(abs(x-bb['x']),abs(y-bb['y']))<=r)
                    rows.append((f.split('/')[-1],t['turn'],bb['type'][:3],W(prev,me,1),W(prev,me,3),W(prev,op,1),W(prev,op,3)))
        prev=s
cat=collections.Counter()
for r in rows:
    my1,my3,en1,en3=r[3:]
    if en3==0: c='lone-F (no enemy W within 3)'
    elif en1==0: c='enemy W near(<=3) but not adjacent'
    elif my3>=en3*1.5: c='outnumbered locally? no: I had 1.5x within 3'
    elif my3>=en3: c='roughly equal within 3'
    else: c='outnumbered'
    cat[c]+=1
print(len(rows),'losses (T25-60)'); 
for k,v in cat.most_common(): print(' ',v,k)
