import json,sys
d=json.load(open(sys.argv[1])); me=sys.argv[2]; op='K' if me=='Y' else 'Y'
lo=int(sys.argv[3]) if len(sys.argv)>3 else 0; hi=int(sys.argv[4]) if len(sys.argv)>4 else 999
bl={b['id']:b for b in d['map']['buildings']}
prev=None
def units_at(state,x,y):
    r={}
    for tm,k,ux,uy,c in state['units']:
        if ux==x and uy==y: r[tm+k]=c
    return r
turns=d['turns']
prev_state=None
for t in turns:
    s=t['state']
    if prev_state:
        po={b['id']:b['owner'] for b in prev_state['buildings']}
        for b in s['buildings']:
            if po[b['id']]!=b['owner'] and lo<=t['turn']<=hi:
                bb=bl[b['id']]
                print(f"T{t['turn']} {bb['type'][:3]}({bb['x']},{bb['y']}) {po[b['id']]}->{b['owner']} before={units_at(prev_state,bb['x'],bb['y'])} after={units_at(s,bb['x'],bb['y'])}")
    prev_state=s
