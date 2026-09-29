#!/usr/bin/env python3
"""턴별 지도 뷰: python3 vw.py replay.json T0 [T1]  (W 스택 표시, 건물 소유)"""
import json, sys
d = json.load(open(sys.argv[1]))
t0 = int(sys.argv[2]); t1 = int(sys.argv[3]) if len(sys.argv) > 3 else t0
bs = d['map']['buildings']
pos = {b['id']:(b['x'],b['y']) for b in bs}
ty = {b['id']:b['type'] for b in bs}
sc = {b['id']:b['score'] for b in bs}
terr = d['map']['terrain']
for t in d['turns']:
    n = t['turn']
    if n < t0 or n > t1: continue
    s = t['state']
    own = {b['id']:b['owner'] for b in s['buildings']}
    grid = [[' . ' if terr[y][x]=='.' else ('###' if terr[y][x]=='#' else ' . ') for x in range(15)] for y in range(15)]
    for y in range(15):
        for x in range(15):
            if terr[y][x]=='H': grid[y][x]=' H '
    for i,(x,y) in pos.items():
        o = own[i].lower() if own[i]!='N' else '_'
        grid[y][x] = f"{ty[i][0]}{sc[i]}{o}" if False else f"{ty[i][:1]}{sc[i]}{o}"
    cell = {}
    for tm,k,x,y,c in s['units']:
        cell.setdefault((x,y),{}).setdefault(tm,{})[k]=c
    print(f"--- turn {n} res {s['resources']} events {[ (e['type'][:3],e['x'],e['y'],e.get('team','')) for e in t['events']]}")
    print("cmds Y:", [c for c in t['commands']['Y'] if not c.startswith('PRIO')][:14])
    print("cmds K:", [c for c in t['commands']['K'] if not c.startswith('PRIO')][:14])
    for y in range(15):
        row = ''
        for x in range(15):
            g = grid[y][x]
            u = cell.get((x,y))
            if u:
                a = u.get('Y',{}); b = u.get('K',{})
                txt = ''
                if a: txt += f"Y{a.get('W',0)}" + ('f' if a.get('F') else '')
                if b: txt += f"K{b.get('W',0)}" + ('f' if b.get('F') else '')
                row += f"{g}[{txt:<7}]"
            else:
                row += f"{g}          "[:14] if False else f"{g}[       ]"
        print(row)
