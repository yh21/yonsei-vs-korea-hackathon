#!/usr/bin/env python3
"""run hunter vs opp over seeds (both sides), keep all replays, print early/late metrics."""
import sys, os, json, subprocess, argparse, re
from concurrent.futures import ThreadPoolExecutor
ROOT='/Users/yh/Desktop/yonsei-vs-korea-hackathon'
ap=argparse.ArgumentParser()
ap.add_argument('--bot',default='./v18-work/zoo/hunter/bot'); ap.add_argument('--opp',default='./bot8')
ap.add_argument('--seeds',nargs=2,type=int,default=[10000,6]); ap.add_argument('--out',default='v18-work/zoo/hunter/tmp/all')
ap.add_argument('--workers',type=int,default=2); ap.add_argument('--env',default='')
a=ap.parse_args()
os.makedirs(os.path.join(ROOT,a.out),exist_ok=True)
env=dict(os.environ)
for kv in a.env.split():
    k,v=kv.split('='); env[k]=v
def play(job):
    seed,side=job
    y,k=(a.bot,a.opp) if side=='Y' else (a.opp,a.bot)
    rp=os.path.join(ROOT,a.out,f'{os.path.basename(a.opp)}_{seed}_{side}.json')
    subprocess.run(['python3','yk-development-tools/bots/dist/starter/play_local.py','--bot',y,'--opponent',k,'--seed',str(seed),'--replay',rp],capture_output=True,text=True,cwd=ROOT,env=env)
    d=json.load(open(rp)); sc={b['id']:b['score'] for b in d['map']['buildings']}
    me=side; op='K' if side=='Y' else 'Y'
    res=d['result']; w=res['winner']; r='W' if w==me else ('L' if w==op else 'D')
    pts={}
    for t in d['turns']:
        n=t['turn']
        if n in (10,20,30,40,50,60,80,100,120,140):
            p={'Y':0,'K':0}
            for b in t['state']['buildings']:
                if b['owner'] in p: p[b['owner']]+=sc[b['id']]
            pts[n]=p[me]-p[op]
    return dict(seed=seed,side=side,res=r,fin=res['score'][me]-res['score'][op],reason=res['reason'],pts=pts)
jobs=[(s,sd) for s in range(a.seeds[0],a.seeds[0]+a.seeds[1]) for sd in ('Y','K')]
with ThreadPoolExecutor(a.workers) as ex: rs=list(ex.map(play,jobs))
W=sum(r['res']=='W' for r in rs); L=sum(r['res']=='L' for r in rs); D=len(rs)-W-L
print(f'{a.opp}: {W}W {D}D {L}L / {len(rs)}')
for n in (10,20,30,40,50,60,80,100,120,140):
    v=[r['pts'][n] for r in rs if n in r['pts']]
    if v: print(f'  pts diff T{n}: mean {sum(v)/len(v):+.1f} ({len(v)} games)')
print('  final diff mean',round(sum(r['fin'] for r in rs)/len(rs),1))
