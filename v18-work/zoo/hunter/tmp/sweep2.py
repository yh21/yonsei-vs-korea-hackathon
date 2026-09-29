#!/usr/bin/env python3
import sys,subprocess,re,os
cfgs=sys.argv[1:]
opps=os.environ.get('OPPS','bot8,bot13').split(',')
n=os.environ.get('NSEEDS','30')
for cfg in cfgs:
    parts=[]
    for o in opps:
        r=subprocess.run(['python3','v18-work/zoo/hunter/tmp/evalgames.py','--seeds','10000',n,'--workers','3','--bot','./v18-work/zoo/hunter/bot_tune','--opp','./'+o,'--env',cfg,'--out','v18-work/zoo/hunter/tmp/sw_'+o],capture_output=True,text=True,cwd='/Users/yh/Desktop/yonsei-vs-korea-hackathon')
        lines=r.stdout.strip().split('\n')
        m=re.search(r'(\d+)W (\d+)D (\d+)L',lines[0])
        fin=[l for l in lines if 'final' in l]
        f=re.search(r'mean ([-+\d.]+)',fin[0]).group(1) if fin else '?'
        parts.append(f"{o}: {m.group(1)}W {m.group(3)}L fin {f}")
    print(f"{cfg or 'base':36s} | "+' | '.join(parts),flush=True)
