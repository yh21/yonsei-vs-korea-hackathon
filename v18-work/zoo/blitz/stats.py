import json,sys,glob
def stats(path):
    d=json.load(open(path))
    me='Y' if d['teams']['Y']['name']=='bot' else 'K'
    ty={b['id']:b['type'] for b in d['map']['buildings']}
    firstcap={}
    tele=0;m2=0;spawnH=0;spawnS=0
    for t in d['turns']:
        for c in t['commands'][me]:
            if c.startswith('TELE'): tele+=1
            if c.startswith('MOVE2'): m2+=1
            if c.startswith('SPAWN') and len(c.split())==5: spawnH+=1
            if c.startswith('SPAWN S'): spawnS+=1
        for b in t['state']['buildings']:
            if b['owner']==me and b['id'] not in firstcap:
                firstcap[b['id']]=t['turn']
    fc={}
    for i,tn in firstcap.items(): fc.setdefault(ty[i],[]).append(tn)
    print(path.split('/')[-1], d['result']['winner'], 'tele',tele,'move2',m2,'hospSpawn',spawnH,'scoutSpawn',spawnS, {k:sorted(v) for k,v in fc.items()})
for p in sys.argv[1:]: stats(p)
