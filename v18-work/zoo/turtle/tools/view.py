import json,sys
# usage: view.py replay from to [step]
d=json.load(open(sys.argv[1])); a=int(sys.argv[2]); b=int(sys.argv[3]); st=int(sys.argv[4]) if len(sys.argv)>4 else 1
sc={x['id']:x for x in d['map']['buildings']}
for t in d['turns']:
    n=t['turn']
    if n<a or n>b or (n-a)%st: continue
    s=t['state']
    own={}
    for bd in s['buildings']:
        if bd['owner'] in ('Y','K'): own.setdefault(bd['owner'],[]).append('%s%d'%(sc[bd['id']]['type'][:3],bd['id']))
    print('T',n,'res',s['resources'])
    print('  Y own',own.get('Y'),' K own',own.get('K'))
    us={'Y':[],'K':[]}
    for tm,k,x,y,c in s['units']: us[tm].append('%s%d@%d,%d'%(k,c,x,y))
    print('  Y',' '.join(us['Y']))
    print('  K',' '.join(us['K']))
    print('  cmdY',t['commands']['Y'][:6]); 
