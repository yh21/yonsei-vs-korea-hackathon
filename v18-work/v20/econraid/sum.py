import json,sys,os
for f in sys.argv[1:]:
    try: d=json.load(open(f))
    except Exception as e: print(f,'ERR',e); continue
    tot=0;g=0
    parts=[]
    for opp,o in d.items():
        name=os.path.basename(os.path.dirname(opp)) if opp.endswith('/bot') else os.path.basename(opp)
        parts.append(f"{name}:{o['win']+o['draw']*0.5:g}/{o['games']} ff{o['forfeits']} m{o['avg_margin']}")
        tot+=o['win']+o['draw']*0.5; g+=o['games']
    print(os.path.basename(f).ljust(18),' | '.join(parts),f"| ALL {tot:g}/{g}={tot/max(g,1):.3f}")
