import json,sys
def run(fn, me, upto=999, every=10):
    d=json.load(open(fn)); B={b['id']:b for b in d['map']['buildings']}
    pos={(b['x'],b['y']):b for b in d['map']['buildings']}
    en='K' if me=='Y' else 'Y'
    prev={b['id']:'N' for b in d['map']['buildings']}
    print(fn.split('/')[-1], d['result'], 'me=',me)
    for t in d['turns']:
        n=t['turn']
        if n>upto: break
        s=t['state']
        ch=[]
        for b in s['buildings']:
            if b['owner']!=prev[b['id']]:
                bb=B[b['id']]; ch.append(f"{prev[b['id']]}>{b['owner']} {bb['type'][:3]}{bb['score']}@({bb['x']},{bb['y']})")
                prev[b['id']]=b['owner']
        u={'Y':[0,0,0],'K':[0,0,0]}
        for tm,k,x,y,c in s['units']: u[tm]['FWS'.index(k)]+=c
        sp=[c for c in t['commands'][en] if c.startswith('TELE') or (c.startswith('SPAWN') and len(c.split())>4)]
        if ch or n%every==0:
            print(f"T{n:3d} res{s['resources'][me]:2d}/{s['resources'][en]:2d} W me{u[me][1]} en{u[en][1]} F {u[me][0]}/{u[en][0]} | {'; '.join(ch)} | en-spec: {sp[:3]}")
if __name__=="__main__":
    run(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv)>3 else 999, int(sys.argv[4]) if len(sys.argv)>4 else 10)
