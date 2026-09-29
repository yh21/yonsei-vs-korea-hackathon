#!/usr/bin/env python3
import sys,subprocess,re
cfgs=sys.argv[1:]
for cfg in cfgs:
    r=subprocess.run(['python3','v18-work/zoo/hunter/tmp/evalgames.py','--seeds','10000','30','--workers','3','--bot','./v18-work/zoo/hunter/bot_tune','--env',cfg,'--out','v18-work/zoo/hunter/tmp/sw'],capture_output=True,text=True,cwd='/Users/yh/Desktop/yonsei-vs-korea-hackathon')
    lines=r.stdout.strip().split('\n')
    head=lines[0]; fin=[l for l in lines if 'final' in l]
    t50=[l for l in lines if 'T50' in l]
    print(f"{cfg or 'base':40s} {head} | {t50[0].strip() if t50 else ''} | {fin[0].strip() if fin else ''}",flush=True)
