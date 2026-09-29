#!/usr/bin/env python3
"""v18 개발용 아레나: 봇 vs 여러 상대를 양 진영 교대로 대전시키고 요약을 JSON으로 출력.

사용: python3 v18-work/arena.py --bot ./bot17 --opp ./bot15 ./bot16 --seeds 7000 40 [--workers 3] [--keep DIR] [--json]
 - 저장소 루트에서 실행할 것 (봇 경로는 루트 기준).
 - seeds LO N : 시드 LO..LO+N-1 를 각각 (bot=Y) / (bot=K) 두 번씩 => 2N 판.
 - 승률은 무승부 0.5로 계산. forfeit(시간초과/크래시)는 별도 집계 (CPU 과부하 주의: workers<=3 권장).
 - --keep DIR 를 주면 진 판의 리플레이(JSON)를 DIR에 저장한다.
"""
import argparse, ast, json, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNNER = os.path.join(ROOT, "yk-development-tools/bots/dist/starter/play_local.py")
LINE = re.compile(r"승자=(\S+) 종료=(\S+) 턴=(\d+) 점수=(\{.*?\})")

def play(bot, opp, seed, bot_is_y, keep):
    y, k = (bot, opp) if bot_is_y else (opp, bot)
    rp = os.devnull
    if keep:
        os.makedirs(keep, exist_ok=True)
        rp = os.path.join(keep, f"{os.path.basename(bot)}_vs_{os.path.basename(opp)}_{seed}_{'Y' if bot_is_y else 'K'}.json")
    r = subprocess.run(["python3", RUNNER, "--bot", y, "--opponent", k, "--seed", str(seed), "--replay", rp],
                       capture_output=True, text=True, cwd=ROOT)
    m = LINE.search(r.stdout)
    if not m:
        return dict(seed=seed, side="Y" if bot_is_y else "K", result="error", detail=(r.stdout + r.stderr)[-300:], rp=rp)
    w, reason, turns, sc = m.group(1), m.group(2), int(m.group(3)), ast.literal_eval(m.group(4))
    me = "Y" if bot_is_y else "K"; en = "K" if bot_is_y else "Y"
    res = "win" if w == me else ("loss" if w == en else "draw")
    return dict(seed=seed, side=me, result=res, reason=reason, turns=turns, my=sc[me], opp=sc[en], rp=rp)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bot", required=True)
    ap.add_argument("--opp", nargs="+", required=True)
    ap.add_argument("--seeds", nargs=2, type=int, default=[7000, 40], metavar=("LO", "N"))
    ap.add_argument("--workers", type=int, default=3)
    ap.add_argument("--keep", default=None)
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    lo, n = a.seeds
    out = {}
    for opp in a.opp:
        jobs = [(s, side) for s in range(lo, lo + n) for side in (True, False)]
        with ThreadPoolExecutor(a.workers) as ex:
            rs = list(ex.map(lambda j: play(a.bot, opp, j[0], j[1], a.keep), jobs))
        # 이긴 판의 리플레이는 지운다
        if a.keep:
            for r in rs:
                if r.get("result") != "loss" and r.get("rp") and os.path.exists(r["rp"]):
                    os.remove(r["rp"])
        w = sum(r["result"] == "win" for r in rs); d = sum(r["result"] == "draw" for r in rs)
        l = sum(r["result"] == "loss" for r in rs); e = sum(r["result"] == "error" for r in rs)
        ff = sum(r.get("reason") == "forfeit" for r in rs)
        wy = sum(r["result"] == "win" and r["side"] == "Y" for r in rs); wk = w - wy
        margin = [r["my"] - r["opp"] for r in rs if "my" in r]
        out[opp] = dict(games=len(rs), win=w, draw=d, loss=l, error=e, forfeits=ff,
                        winrate=round((w + 0.5 * d) / len(rs), 3), win_as_Y=wy, win_as_K=wk,
                        avg_margin=round(sum(margin) / max(1, len(margin)), 2),
                        lost=[(r["seed"], r["side"], r["my"], r["opp"], r["reason"]) for r in rs if r["result"] == "loss"][:40],
                        errors=[r["detail"] for r in rs if r["result"] == "error"][:2])
    if a.json:
        print(json.dumps(out, ensure_ascii=False))
    else:
        for opp, o in out.items():
            print(f"{a.bot} vs {opp}: {o['win']}W {o['draw']}D {o['loss']}L / {o['games']}  winrate={o['winrate']}  (Y승 {o['win_as_Y']}, K승 {o['win_as_K']})  avg_margin={o['avg_margin']}  forfeits={o['forfeits']} errors={o['error']}")
            if o["loss"]: print("   lost(seed,side,my,opp,reason):", o["lost"][:12])

if __name__ == "__main__":
    main()
