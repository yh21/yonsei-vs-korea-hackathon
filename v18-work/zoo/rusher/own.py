import json,sys
d=json.load(open(sys.argv[1])); lo=int(sys.argv[2]); hi=int(sys.argv[3]); step=int(sys.argv[4]) if len(sys.argv)>4 else 1
m=d['map']
print('ids   ',' '.join('%2d'%b['id'] for b in m['buildings']))
print('type  ',' '.join('%2s'%b['type'][:2] for b in m['buildings']))
print('score ',' '.join('%2d'%b['score'] for b in m['buildings']))
for t in d['turns']:
    if lo<=t['turn']<=hi and (t['turn']-lo)%step==0:
        s=t['state']
        row=' '.join(' %s'%(b['owner'] if b['owner']!='N' else '.') for b in s['buildings'])
        u={'Y':[0,0,0],'K':[0,0,0]}
        for tm,k,x,y,c in s['units']: u[tm]['FWS'.index(k)]+=c
        print('T%3d  '%t['turn'],row,' Y',u['Y'][:2],'K',u['K'][:2])
