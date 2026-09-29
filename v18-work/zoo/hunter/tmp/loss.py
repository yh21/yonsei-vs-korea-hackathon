import json,sys
def run(f):
    d=json.load(open(f)); me='Y' if f.endswith('_Y.json') else 'K'; op='K' if me=='Y' else 'Y'
    bl={b['id']:b for b in d['map']['buildings']}
    prev=None; rows=[]
    for t in d['turns']:
        s=t['state']
        if prev:
            po={b['id']:b['owner'] for b in prev['buildings']}
            for b in s['buildings']:
                if po[b['id']]==me and b['owner']!=me and t['turn']<=60:
                    bb=bl[b['id']]
                    def W(state,team,r):
                        return sum(c for tm,k,x,y,c in state['units'] if tm==team and k=='W' and max(abs(x-bb['x']),abs(y-bb['y']))<=r)
                    rows.append((t['turn'],bb['type'][:3],(bb['x'],bb['y']),'myW<=1:',W(prev,me,1),'myW<=3:',W(prev,me,3),'enW<=1:',W(prev,op,1),'enW<=3:',W(prev,op,3)))
        prev=s
    return rows
for f in sys.argv[1:]:
    print(f.split('/')[-1])
    for r in run(f): print('  ',*r)
