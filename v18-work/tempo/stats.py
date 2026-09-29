#!/usr/bin/env python3
"""리플레이 디렉터리 통계. python3 stats.py DIR [me_name_substring]
각 판마다: 결과, 최종점수, 내(=Y/K 중 bot) 기준 hall/eng 보유 비율, 40/80턴 W차, 점수차 등.
파일명: {opp}_{seed}_{side}.json (side = 내 봇 진영)."""
import json, sys, os, glob, re

d = sys.argv[1]
rows = []
for f in sorted(glob.glob(os.path.join(d, "*.json"))):
    m = re.match(r"(.+)_(\d+)_([YK])\.json", os.path.basename(f))
    if not m:
        continue
    opp, seed, side = m.group(1), int(m.group(2)), m.group(3)
    try:
        r = json.load(open(f))
    except Exception:
        continue
    en = 'K' if side == 'Y' else 'Y'
    ty = {b['id']: b['type'] for b in r['map']['buildings']}
    sc = {b['id']: b['score'] for b in r['map']['buildings']}
    res = r['result']
    win = 'W' if res['winner'] == side else ('L' if res['winner'] == en else 'D')
    hall = {'me': 0, 'op': 0}
    eng = {'me': 0, 'op': 0}
    income = {'me': 0, 'op': 0}
    wdiff = {}
    pdiff = {}
    fcount = {}
    n = 0
    for t in r['turns']:
        s = t['state']
        own = {b['id']: b['owner'] for b in s['buildings']}
        for i, o in own.items():
            if ty[i] == 'HALL':
                if o == side: hall['me'] += 1
                if o == en: hall['op'] += 1
            if ty[i] == 'ENG':
                if o == side: eng['me'] += 1
                if o == en: eng['op'] += 1
        n += 1
        w = {'Y': 0, 'K': 0}
        fl = {'Y': 0, 'K': 0}
        for tm, k, x, y, c in s['units']:
            if k == 'W': w[tm] += c
            if k == 'F': fl[tm] += c
        p = {'Y': 0, 'K': 0}
        for i, o in own.items():
            if o in p: p[o] += sc[i]
        if t['turn'] in (20, 40, 60, 80, 100, 120, 140, 160):
            wdiff[t['turn']] = w[side] - w[en]
            pdiff[t['turn']] = p[side] - p[en]
            fcount[t['turn']] = fl[side]
    rows.append((opp, seed, side, win, res['reason'], res['score'][side], res['score'][en], hall['me'], hall['op'], eng['me'], eng['op'], wdiff, pdiff))

print("opp seed side res reason my opp | hallT me/op | engT me/op | Wdiff@40,80,120 | Pdiff@40,80,120")
for r in rows:
    opp, seed, side, win, reason, a, b, hm, ho, em, eo, wd, pd = r
    print(f"{opp:6s} {seed} {side} {win} {reason[:5]:5s} {a:3d}:{b:3d} | {hm:3d}/{ho:3d} | {em:3d}/{eo:3d} | "
          f"{wd.get(40,'-'):>4}{wd.get(80,'-'):>5}{wd.get(120,'-'):>5} | {pd.get(40,'-'):>3}{pd.get(80,'-'):>4}{pd.get(120,'-'):>4}")
