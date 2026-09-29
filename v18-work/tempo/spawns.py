#!/usr/bin/env python3
"""판별 F/W 생산량 합계. python3 spawns.py DIR"""
import json, glob, os, re, sys, collections
d = sys.argv[1]
agg = collections.defaultdict(lambda: [0,0,0,0,0,0])  # by result: nF_me nW_me nF_op nW_op n
for f in sorted(glob.glob(os.path.join(d, "*.json"))):
    m = re.match(r"(.+)_(\d+)_([YK])\.json", os.path.basename(f))
    if not m: continue
    opp, seed, side = m.group(1), int(m.group(2)), m.group(3)
    r = json.load(open(f)); en = 'K' if side=='Y' else 'Y'
    tot = {'Y':{'F':0,'W':0,'S':0},'K':{'F':0,'W':0,'S':0}}
    for t in r['turns']:
        for tm in 'YK':
            for a in t['applied'][tm]:
                if a.get('cmd')=='Spawn': tot[tm][a['kind']] += a['count']
    res = r['result']; win = 'W' if res['winner']==side else ('L' if res['winner']==en else 'D')
    key=(opp,win)
    a = agg[key]; a[0]+=tot[side]['F']; a[1]+=tot[side]['W']; a[2]+=tot[en]['F']; a[3]+=tot[en]['W']; a[4]+=1
for k in sorted(agg):
    a=agg[k]; n=a[4]
    print(k, n, 'me F %.1f W %.0f | opp F %.1f W %.0f'%(a[0]/n,a[1]/n,a[2]/n,a[3]/n))
