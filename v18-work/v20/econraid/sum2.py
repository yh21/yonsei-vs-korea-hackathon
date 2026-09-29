import json,sys,glob,os
tags=sys.argv[1:]
for tag in tags:
    tot=0;g=0;parts=[];lows=[]
    for f in sorted(glob.glob(f"v18-work/v20/econraid/res/{tag}__*.json")):
        try: d=json.load(open(f))
        except Exception: continue
        for opp,o in d.items():
            name=f.split('__')[1][:-5]
            w=o['win']+0.5*o['draw']
            parts.append(f"{name}:{w:g}/{o['games']}{'' if not o['forfeits'] else ' ff'+str(o['forfeits'])}")
            tot+=w;g+=o['games'];lows.append(w/o['games'])
    if g: print(tag.ljust(9),' '.join(parts),f"| ALL {tot:g}/{g}={tot/g:.3f} min={min(lows):.2f}")
