import json,sys
d=json.load(open(sys.argv[1])); every=int(sys.argv[2]) if len(sys.argv)>2 else 5
bl=d['map']['buildings']
print(d['teams']['Y']['name'],'(Y) vs',d['teams']['K']['name'],'(K)',d['result'])
print('ids:',' '.join(f"{b['id']}:{b['type'][:3]}{b['score']}@{b['x']},{b['y']}" for b in bl))
for t in d['turns']:
    n=t['turn']
    if n%every and n>3: continue
    if n>3 and False: continue
    s=t['state']; ow={b['id']:b['owner'] for b in s['buildings']}
    print(f"{n:3d} "+''.join(ow[b['id']].replace('N','.') for b in bl)+f"  res {s['resources']['Y']:2d}/{s['resources']['K']:2d}")
