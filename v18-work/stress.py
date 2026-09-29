#!/usr/bin/env python3
"""스트레스 검사: 턴 제한을 줄여(기본 100ms) 여러 상대와 대전. 몰수패(forfeit)/오류가 0이어야 통과.
사용: python3 v18-work/stress.py <봇경로> [--timeout 100] [--seeds LO N] [--opp ...] [--workers 2]
Y/K 양쪽에서 실행. 최대 RSS(대략)도 보고.
"""
import argparse, os, resource, subprocess, sys, json, tempfile
from concurrent.futures import ThreadPoolExecutor
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "yk-development-tools")
ap = argparse.ArgumentParser()
ap.add_argument("bot"); ap.add_argument("--timeout", type=int, default=100)
ap.add_argument("--seeds", nargs=2, type=int, default=[50000, 6]); ap.add_argument("--workers", type=int, default=2)
ap.add_argument("--opp", nargs="+", default=["./bot17", "./bot15", "./bot_opp", "./v18-work/zoo/rusher/bot", "./v18-work/zoo/turtle/bot", "./v18-work/zoo/hunter/bot", "./v18-work/zoo/blitz/bot"])
a = ap.parse_args()
absb = lambda p: os.path.abspath(os.path.join(ROOT, p))
def game(job):
    opp, seed, botY = job
    y, k = (absb(a.bot), absb(opp)) if botY else (absb(opp), absb(a.bot))
    with tempfile.NamedTemporaryFile(suffix=".json", delete=True) as rf, tempfile.NamedTemporaryFile(suffix=".json", delete=True) as res:
        r = subprocess.run([sys.executable, "-m", "runner", "--seed", str(seed), "--bot-y", y, "--bot-k", k,
                            "--replay", rf.name, "--result", res.name, "--turn-timeout-ms", str(a.timeout)],
                           cwd=TOOLS, capture_output=True, text=True)
        try: out = json.load(open(res.name))
        except Exception: return dict(opp=opp, seed=seed, side="Y" if botY else "K", err=(r.stderr or r.stdout)[-300:])
    me = "Y" if botY else "K"
    return dict(opp=opp, seed=seed, side=me, winner=out.get("winner"), reason=out.get("reason"), forfeit=out.get("forfeit"))
jobs = [(o, s, b) for o in a.opp for s in range(a.seeds[0], a.seeds[0] + a.seeds[1]) for b in (True, False)]
with ThreadPoolExecutor(a.workers) as ex: rs = list(ex.map(game, jobs))
bad = [r for r in rs if r.get("err") or r.get("reason") == "forfeit" or r.get("forfeit")]
by = {}
for r in rs:
    d = by.setdefault(r["opp"], [0, 0, 0]); d[0] += 1
    if r.get("winner") == r["side"]: d[1] += 1
    if r.get("winner") is None: d[2] += 1
for o, (n, w, dr) in by.items(): print(f"{a.bot} vs {o}: {w}/{n} wins")
print(f"timeout={a.timeout}ms games={len(rs)} forfeits/errors={len(bad)} maxrss_children={resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss/1e6:.0f}MB(macOS bytes)")
for r in bad[:8]: print("BAD", r)
sys.exit(1 if bad else 0)
