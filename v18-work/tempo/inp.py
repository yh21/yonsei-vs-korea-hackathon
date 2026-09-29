#!/usr/bin/env python3
"""리플레이 상태 -> 봇 입력 텍스트. python3 inp.py replay.json TURN(봇이 받을 턴 번호) TEAM > input.txt
 (INIT 블록 + 해당 TURN 블록. state 는 직전 턴 종료 상태)"""
import json, sys
d = json.load(open(sys.argv[1])); T = int(sys.argv[2]); team = sys.argv[3]
terr = d['map']['terrain']; bs = d['map']['buildings']
bases = {}
for y, r in enumerate(terr):
    for x, c in enumerate(r):
        if c == 'H': bases[(x, y)] = 1
# bases order: Y is sinchon (x smaller)
bl = sorted(bases.keys())
by = bl[0]; bk = bl[1]
out = ["INIT 15 15", f"TEAM {team}"]
for r in terr: out.append("MAP " + r)
out.append(f"BUILDINGS {len(bs)}")
for b in bs: out.append(f"{b['id']} {b['x']} {b['y']} {b['type']}")
out.append(f"BASE Y {by[0]} {by[1]}"); out.append(f"BASE K {bk[0]} {bk[1]}"); out.append("END")
st = d['turns'][T - 2]['state'] if T >= 2 else None
res = st['resources']
opp = 'K' if team == 'Y' else 'Y'
out.append(f"TURN {T}"); out.append(f"RESOURCE {res[team]} {res[opp]}")
units = sorted(st['units'], key=lambda u: ({'Y': 0, 'K': 1}[u[0]], 'FWS'.index(u[1]), u[3], u[2]))
out.append(f"UNITS {len(units)}")
for tm, k, x, y, c in units: out.append(f"{tm} {k} {x} {y} {c}")
out.append(f"BUILDINGS {len(bs)}")
rev = set(st['revealed'][team])
stg = {b['id']: b for b in st['buildings']}
for b in bs:
    s = stg[b['id']]
    out.append(f"{b['id']} {b['x']} {b['y']} {b['type']} {s['owner']} {s['stage']} {b['score'] if b['id'] in rev else -1}")
out.append("END")
print("\n".join(out))
