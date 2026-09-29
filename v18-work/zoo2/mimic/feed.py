#!/usr/bin/env python3
"""실서버 리플레이(real_XX.json)의 관측(K 시점)을 우리 옛 봇 실행파일에 그대로 먹여 기록된 명령과 일치하는지 본다 -> 어떤 버전이 그 판을 뛰었는지 식별.
python3 feed.py <replay> <bot> [offset]"""
import json, subprocess, sys, os
HDR = int(os.environ.get('HDR','1'))
def blocks(d, off, maxturn):
    T = d['turns']; ob0 = T[0]['observation']
    mp = ob0['map']
    init = ["INIT 15 15", f"TEAM {d['side']}"] + [f"MAP {r}" for r in mp]
    bl = ob0['buildings']
    init.append(f"BUILDINGS {len(bl)}")
    for b in bl: init.append(f"{b['id']} {b['x']} {b['y']} {b['type']}")
    H = [(x, y) for y in range(15) for x in range(15) if mp[y][x] == 'H']
    init.append(f"BASE Y {H[0][0]} {H[0][1]}"); init.append(f"BASE K {H[1][0]} {H[1][1]}"); init.append("END")
    yield "\n".join(init) + "\n"
    for n in range(1, min(len(T), maxturn + 1)):
        ob = T[n - 1 + off]['observation'] if n - 1 + off >= 0 else T[0]['observation']
        L = [f"TURN {ob['turn'] + HDR}", f"RESOURCE {ob['resources'][d['side']]} {ob['resources']['Y' if d['side']=='K' else 'K']}"]
        us = sorted(ob['units'], key=lambda u: (u[0] != 'Y', 'FWS'.index(u[1]), u[3], u[2]))
        L.append(f"UNITS {len(us)}")
        for tm, k, x, y, c in us: L.append(f"{tm} {k} {x} {y} {c}")
        L.append(f"BUILDINGS {len(ob['buildings'])}")
        for b in sorted(ob['buildings'], key=lambda b: b['id']):
            L.append(f"{b['id']} {b['x']} {b['y']} {b['type']} {b['owner']} {b['stage']} {b.get('score', -1)}")
        L.append("END")
        yield "\n".join(L) + "\n"
def main():
    rp, bot = sys.argv[1], sys.argv[2]; off = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    maxturn = int(sys.argv[4]) if len(sys.argv) > 4 else 12
    d = json.load(open(rp)); T = d['turns']
    pr = subprocess.Popen([bot], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
    it = blocks(d, off, maxturn)
    pr.stdin.write(next(it)); pr.stdin.flush()
    match = tot = 0; det = []
    for i, blk in enumerate(it, start=1):
        pr.stdin.write(blk); pr.stdin.flush()
        out = []
        while True:
            l = pr.stdout.readline().strip()
            if l == "END" or l == "": break
            out.append(l)
        rec = (T[i].get('command') or {}).get('lines') or []
        rs = sorted(x for x in rec if not x.startswith('PRIORITY')); os_ = sorted(x for x in out if not x.startswith('PRIORITY'))
        tot += 1; match += (rs == os_)
        det.append((i, rs == os_))
    pr.stdin.close(); pr.terminate()
    print(f"{bot} off={off}: {match}/{tot} turns identical")
    return match, tot
if __name__ == "__main__": main()
