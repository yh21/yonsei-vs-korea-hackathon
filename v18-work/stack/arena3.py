#!/usr/bin/env python3
"""in-process arena: bot vs opponents, reports win/loss, forfeits, max turn ms.
usage: python3 v18-work/stack/arena3.py --bot ./X --opp ./bot17 ./bot16 --seeds 10000 40 [--sides Y|K|YK] [--workers 4] [--keep DIR] [--json]"""
import argparse, json, os, sys
from concurrent.futures import ProcessPoolExecutor
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
KIT = os.path.join(ROOT, "yk-development-tools/bots/dist/starter")
sys.path.insert(0, os.path.join(ROOT, "yk-development-tools"))
sys.path.insert(0, KIT)

def play(args):
    bot, opp, seed, bot_is_y, keep, tmo = args
    from _support import turn_timeout_ms, first_turn_timeout_ms
    from engine.config import load_config
    from runner import SubprocessBot, run_match
    y, k = (bot, opp) if bot_is_y else (opp, bot)
    by = SubprocessBot(y, name=y); bk = SubprocessBot(k, name=k)
    try:
        cfg = load_config(); cfg["first_turn_timeout_ms"] = max(first_turn_timeout_ms(), tmo or 0)
        replay, result = run_match(seed, cfg, by, bk, turn_timeout_ms=(tmo or turn_timeout_ms()))
    finally:
        by.close(); bk.close()
    me = "Y" if bot_is_y else "K"; en = "K" if bot_is_y else "Y"
    w = result["winner"]
    res = "win" if w == me else ("loss" if w == en else "draw")
    mymax = (by if bot_is_y else bk).max_turn_ms
    def fl(team):
        sp = 0
        for t in replay["turns"]:
            for c in t["applied"][team]:
                if c["cmd"] == "Spawn" and c["kind"] == "F": sp += c["count"]
        alive = sum(c for (tm, k, x, y, c) in replay["turns"][-1]["state"]["units"] if tm == team and k == "F")
        return sp - alive
    myfl, opfl = fl(me), fl(en)
    rp = None
    if keep and res != "win":
        os.makedirs(keep, exist_ok=True)
        rp = os.path.join(keep, f"{os.path.basename(bot)}_vs_{os.path.basename(opp)}_{seed}_{me}.json")
        json.dump(replay, open(rp, "w"), ensure_ascii=False)
    return dict(seed=seed, side=me, result=res, reason=result["reason"], turns=result["turns"],
                my=result["score"][me], opp=result["score"][en], maxms=round(mymax, 1), rp=rp, myfl=myfl, opfl=opfl,
                forfeit=result.get("forfeit"))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bot", required=True); ap.add_argument("--opp", nargs="+", required=True)
    ap.add_argument("--seeds", nargs=2, type=int, default=[10000, 40]); ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--sides", default="YK"); ap.add_argument("--keep", default=None); ap.add_argument("--json", action="store_true"); ap.add_argument("--tmo", type=int, default=0, help="dev: per-turn timeout ms (0=official)")
    a = ap.parse_args()
    lo, n = a.seeds
    sides = [True] if a.sides == "Y" else [False] if a.sides == "K" else [True, False]
    out = {}
    with ProcessPoolExecutor(a.workers) as ex:
        for opp in a.opp:
            jobs = [(a.bot, opp, s, sd, a.keep, a.tmo) for s in range(lo, lo + n) for sd in sides]
            rs = list(ex.map(play, jobs))
            w = sum(r["result"] == "win" for r in rs); d = sum(r["result"] == "draw" for r in rs); l = sum(r["result"] == "loss" for r in rs)
            ff = [r for r in rs if r["reason"] == "forfeit"]
            inst = sum(r["result"] == "loss" and r["reason"] == "instant" for r in rs)
            out[opp] = dict(games=len(rs), win=w, draw=d, loss=l, winrate=round((w + .5 * d) / len(rs), 3), forfeits=len(ff),
                            instant_losses=inst, avg_margin=round(sum(r["my"] - r["opp"] for r in rs) / len(rs), 2),
                            maxms=max(r["maxms"] for r in rs), myfl=round(sum(r["myfl"] for r in rs)/len(rs),1), opfl=round(sum(r["opfl"] for r in rs)/len(rs),1), avgmaxms=round(sum(r["maxms"] for r in rs) / len(rs), 1),
                            lost=[(r["seed"], r["side"], r["my"], r["opp"], r["reason"]) for r in rs if r["result"] == "loss"],
                            ff=[(r["seed"], r["side"], r["forfeit"]) for r in ff])
    if a.json:
        print(json.dumps(out, ensure_ascii=False))
    else:
        for opp, o in out.items():
            print(f"{os.path.basename(a.bot)} vs {os.path.basename(opp)}: {o['win']}W {o['draw']}D {o['loss']}L /{o['games']} wr={o['winrate']} margin={o['avg_margin']} ff={o['forfeits']} inst={o['instant_losses']} maxms={o['maxms']} avgmax={o['avgmaxms']} Flost={o['myfl']}/{o['opfl']}")
            if o["loss"]: print("   lost:", o["lost"][:14])
            if o["ff"]: print("   FORFEITS:", o["ff"])
if __name__ == "__main__":
    main()
