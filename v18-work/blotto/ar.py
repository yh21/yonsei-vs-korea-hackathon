#!/usr/bin/env python3
"""blotto dev arena: bot vs opps over seeds. --side Y|K|both. Prints compact summary."""
import argparse, ast, json, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RUNNER = os.path.join(ROOT, "yk-development-tools/bots/dist/starter/play_local.py")
LINE = re.compile(r"승자=(\S+) 종료=(\S+) 턴=(\d+) 점수=(\{.*?\})")
RETRIES = {"n": 0}
def play(bot, opp, seed, bot_is_y, keep):
    for attempt in range(4):
        r = play1(bot, opp, seed, bot_is_y, keep)
        if r.get("reason") != "forfeit": return r
        RETRIES["n"] += 1
    return r
def play1(bot, opp, seed, bot_is_y, keep):
    y, k = (bot, opp) if bot_is_y else (opp, bot)
    rp = os.devnull
    if keep:
        os.makedirs(keep, exist_ok=True)
        rp = os.path.join(keep, f"{os.path.basename(os.path.dirname(bot)) or 'b'}_vs_{os.path.basename(opp)}_{seed}_{'Y' if bot_is_y else 'K'}.json")
    r = subprocess.run(["python3", RUNNER, "--bot", y, "--opponent", k, "--seed", str(seed), "--replay", rp], capture_output=True, text=True, cwd=ROOT)
    m = LINE.search(r.stdout)
    if not m: return dict(seed=seed, side="Y" if bot_is_y else "K", result="error", detail=(r.stdout + r.stderr)[-300:], rp=rp)
    w, reason, turns, sc = m.group(1), m.group(2), int(m.group(3)), ast.literal_eval(m.group(4))
    me = "Y" if bot_is_y else "K"; en = "K" if bot_is_y else "Y"
    res = "win" if w == me else ("loss" if w == en else "draw")
    return dict(seed=seed, side=me, result=res, reason=reason, turns=turns, my=sc[me], opp=sc[en], rp=rp)
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bot", required=True); ap.add_argument("--opp", nargs="+", required=True)
    ap.add_argument("--seeds", nargs=2, type=int, default=[10000, 40]); ap.add_argument("--workers", type=int, default=2)
    ap.add_argument("--side", default="Y"); ap.add_argument("--keep", default=None)
    a = ap.parse_args(); lo, n = a.seeds
    sides = [True] if a.side == "Y" else ([False] if a.side == "K" else [True, False])
    for opp in a.opp:
        jobs = [(s, sd) for s in range(lo, lo + n) for sd in sides]
        with ThreadPoolExecutor(a.workers) as ex: rs = list(ex.map(lambda j: play(a.bot, opp, j[0], j[1], a.keep), jobs))
        if a.keep:
            for r in rs:
                if r.get("result") != "loss" and r.get("rp") and os.path.exists(r["rp"]): os.remove(r["rp"])
        w = sum(r["result"] == "win" for r in rs); d = sum(r["result"] == "draw" for r in rs); l = sum(r["result"] == "loss" for r in rs)
        e = sum(r["result"] == "error" for r in rs); ff = sum(r.get("reason") == "forfeit" for r in rs)
        inst = sum(r["result"] == "loss" and r.get("reason") == "instant" for r in rs)
        mg = [r["my"] - r["opp"] for r in rs if "my" in r]
        print(f"vs {os.path.basename(opp):8s} retries={RETRIES['n']} {w}W {d}D {l}L /{len(rs)} wr={(w+0.5*d)/len(rs):.3f} margin={sum(mg)/max(1,len(mg)):+.1f} forfeit={ff} err={e} annih_losses={inst}")
        lost = [(r["seed"], r["side"], r["my"], r["opp"], r["reason"][:4]) for r in rs if r["result"] == "loss"]
        if lost: print("   lost:", lost[:14])
        if e: print("   errors:", [r.get("detail") for r in rs if r["result"] == "error"][:2])
        sys.stdout.flush()
if __name__ == "__main__": main()
