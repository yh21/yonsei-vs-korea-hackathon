#!/usr/bin/env python3
"""핵심 건물(HALL/ENG/DEPOT) 이벤트 + 전투 요약. python3 ev.py replay [types]"""
import json, sys
d = json.load(open(sys.argv[1]))
types = sys.argv[2].split(',') if len(sys.argv) > 2 else ['HALL','ENG','DEPOT']
ty = {b['id']: b['type'] for b in d['map']['buildings']}
pos = {(b['x'],b['y']): b['id'] for b in d['map']['buildings']}
me = d['teams']['Y']['name'], d['teams']['K']['name']
print(me, d['result'])
for t in d['turns']:
    for e in t['events']:
        if e['type'] in ('capture','neutralize'):
            i = pos.get((e['x'],e['y']))
            if i is not None and ty[i] in types:
                s = t['state']
                u = {'Y':0,'K':0}
                for tm,k,x,y,c in s['units']:
                    if k=='W': u[tm]+=c
                print(f"t{t['turn']:3d} {e['type'][:3]} {ty[i]}{(e['x'],e['y'])} by {e.get('team','?')}  Wtot {u}")
