#!/usr/bin/env python3
"""tempo 분석: 리플레이에서 캡처 타임라인/자원 낭비/병력 추이를 본다.
사용: python3 v18-work/tempo/an.py replay.json [maxturn]"""
import json, sys
d = json.load(open(sys.argv[1]))
mt = int(sys.argv[2]) if len(sys.argv) > 2 else 60
ty = {b['id']: b['type'] for b in d['map']['buildings']}
sc = {b['id']: b['score'] for b in d['map']['buildings']}
pos = {b['id']: (b['x'], b['y']) for b in d['map']['buildings']}
print(d['teams'], d['result'])
print('seed', d['seed'])
for i in sorted(ty):
    print(i, ty[i], pos[i], sc[i])
prev = {i: 'N' for i in ty}
waste = {'Y': 0, 'K': 0}
for t in d['turns']:
    n = t['turn']
    if n > mt:
        break
    s = t['state']
    ev = [e for e in t['events'] if e['type'] in ('capture', 'neutralize')]
    u = {'Y': [0, 0, 0], 'K': [0, 0, 0]}
    for tm, k, x, y, c in s['units']:
        u[tm]['FWS'.index(k)] += c
    line = f"{n:3d} res {s['resources']['Y']:2d}/{s['resources']['K']:2d} FWS Y{u['Y']} K{u['K']}"
    for e in ev:
        b = [i for i in pos if pos[i] == (e['x'], e['y'])][0]
        line += f"  [{e['type'][:3]} {ty[b][:4]}{sc[b]} {e.get('team','?')}]"
    print(line)
