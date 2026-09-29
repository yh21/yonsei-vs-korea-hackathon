#!/usr/bin/env python3
"""내 HALL/ENG 가 상대에게 중립화된 이벤트와 주변 병력. python3 raids.py DIR"""
import json, glob, os, re, sys, collections
d = sys.argv[1]
tot = collections.Counter(); games = 0
rows = []
for f in sorted(glob.glob(os.path.join(d, "*.json"))):
    m = re.match(r"(.+)_(\d+)_([YK])\.json", os.path.basename(f))
    if not m: continue
    opp, seed, side = m.group(1), int(m.group(2)), m.group(3)
    r = json.load(open(f)); en = 'K' if side == 'Y' else 'Y'
    pos = {(b['x'], b['y']): b for b in r['map']['buildings']}
    prev_units = None
    win = 'W' if r['result']['winner'] == side else 'L'
    n_me = n_op = 0
    for t in r['turns']:
        s = t['state']
        for e in t['events']:
            if e['type'] == 'neutralize' and e.get('team') in ('Y', 'K'):
                b = pos.get((e['x'], e['y']))
                if b and b['type'] in ('HALL', 'ENG'):
                    if e['team'] == en: n_me += 1
                    else: n_op += 1
    rows.append((opp, seed, win, n_me, n_op))
import statistics
for w in 'WL':
    rr = [r for r in rows if r[2] == w]
    if rr: print(w, len(rr), 'avg times my HALL/ENG neutralized by enemy: %.2f, I neutralized theirs: %.2f' % (statistics.mean(r[3] for r in rr), statistics.mean(r[4] for r in rr)))
