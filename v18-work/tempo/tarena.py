#!/usr/bin/env python3
"""tempo 전용 아레나. 미러 대칭(방향 정규화)을 이용해 시드당 1판(짝수 시드=Y, 홀수 시드=K)만 둔다.
사용: python3 v18-work/tempo/tarena.py --bot ./v18-work/tempo/bot --opp ./bot17 ./bot16 ./bot15 ./bot_opp --seeds 10000 100 [--workers 2]
      [--both] (양 진영 모두) [--keep DIR] (모든 리플레이 저장) [--env K=V ...] (봇 환경변수, TUNE 빌드용)
결과: 상대별 승/무/패, 승률(무승부 0.5), forfeits, 진 판 시드 목록, 평균 점수차.
"""
import argparse, ast, json, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RUNNER = os.path.join(ROOT, "yk-development-tools/bots/dist/starter/play_local.py")
LINE = re.compile(r"승자=(\S+) 종료=(\S+) 턴=(\d+) 점수=(\{.*?\})")


def play(bot, opp, seed, bot_is_y, keep, env):
    y, k = (bot, opp) if bot_is_y else (opp, bot)
    rp = os.devnull
    if keep:
        os.makedirs(keep, exist_ok=True)
        rp = os.path.join(keep, f"{os.path.basename(opp)}_{seed}_{'Y' if bot_is_y else 'K'}.json")
    e = dict(os.environ)
    e.update(env)
    r = subprocess.run(["python3", RUNNER, "--bot", y, "--opponent", k, "--seed", str(seed), "--replay", rp],
                       capture_output=True, text=True, cwd=ROOT, env=e)
    m = LINE.search(r.stdout)
    me = "Y" if bot_is_y else "K"
    en = "K" if bot_is_y else "Y"
    if not m:
        return dict(seed=seed, side=me, result="error", detail=(r.stdout + r.stderr)[-300:])
    w, reason, turns, sc = m.group(1), m.group(2), int(m.group(3)), ast.literal_eval(m.group(4))
    res = "win" if w == me else ("loss" if w == en else "draw")
    return dict(seed=seed, side=me, result=res, reason=reason, turns=turns, my=sc[me], opp=sc[en])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bot", required=True)
    ap.add_argument("--opp", nargs="+", required=True)
    ap.add_argument("--seeds", nargs=2, type=int, default=[10000, 100], metavar=("LO", "N"))
    ap.add_argument("--workers", type=int, default=2)
    ap.add_argument("--both", action="store_true")
    ap.add_argument("--keep", default=None)
    ap.add_argument("--env", nargs="*", default=[])
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    env = dict(kv.split("=", 1) for kv in a.env)
    lo, n = a.seeds
    for opp in a.opp:
        jobs = []
        for s in range(lo, lo + n):
            if a.both:
                jobs += [(s, True), (s, False)]
            else:
                jobs.append((s, s % 2 == 0))
        with ThreadPoolExecutor(a.workers) as ex:
            rs = list(ex.map(lambda j: play(a.bot, opp, j[0], j[1], a.keep, env), jobs))
        w = sum(r["result"] == "win" for r in rs)
        d = sum(r["result"] == "draw" for r in rs)
        l = sum(r["result"] == "loss" for r in rs)
        e = sum(r["result"] == "error" for r in rs)
        ff = sum(r.get("reason") == "forfeit" for r in rs)
        inst = sum(r["result"] == "loss" and r.get("reason") == "instant" for r in rs)
        margin = [r["my"] - r["opp"] for r in rs if "my" in r]
        lost = [(r["seed"], r["my"], r["opp"]) for r in rs if r["result"] == "loss"]
        print(f"{os.path.basename(a.bot)} vs {opp}: {w}W {d}D {l}L /{len(rs)}  winrate={(w + 0.5 * d) / len(rs):.3f}"
              f"  avg_margin={sum(margin) / max(1, len(margin)):.2f}  wipeouts={inst}  forfeits={ff} errors={e}", flush=True)
        if l:
            print("   lost:", lost, flush=True)
        if e:
            print("   err:", [r["detail"] for r in rs if r["result"] == "error"][:1], flush=True)


if __name__ == "__main__":
    main()
