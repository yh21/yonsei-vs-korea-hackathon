#!/usr/bin/env python3
"""v19 를 가장 잘 이기는 mimic 파라미터를 무작위 탐색. 저장소 루트에서 실행.
python3 v18-work/zoo2/mimic/search.py <base: spread|blob|teleraid> <n_configs> <seed_lo> <n_seeds> <out.jsonl> [rng_seed]"""
import json, os, random, subprocess, sys
base, ncfg, slo, nseeds, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
rng = random.Random(int(sys.argv[6]) if len(sys.argv) > 6 else 1)
M = "./v18-work/zoo2/mimic"
def sample():
    p = {}
    p["MM_FO"] = rng.choice([3, 4, 5, 6, 7, 8, 9, 10])
    p["MM_FL"] = rng.choice([2, 3, 4, 5, 6, 8])
    p["MM_OT"] = rng.choice([4, 6, 8, 10])
    p["MM_TELE"] = rng.choice([0, 1]); p["MM_FWD"] = rng.choice([0, 1, 1])
    p["MM_HUNT"] = rng.choice([40, 70, 100, 130]); p["MM_DEF"] = rng.choice([80, 130, 200])
    p["MM_SEP"] = rng.choice([1, 2, 3, 5]); p["MM_GUARD"] = rng.choice([0, 1, 1]); p["MM_GV"] = rng.choice([40, 70, 90, 120])
    p["MM_RS"] = rng.choice([8, 12, 16, 20, 30, 999]); p["MM_RB"] = rng.choice([0, 50, 100, 150]); p["MM_RN"] = rng.choice([2, 4, 6, 10])
    p["MM_TG"] = rng.choice([1, 2, 3, 5]); p["MM_SB"] = rng.choice([0, 30, 60])
    base_w = [45, 90, 50, 55, 100, 60, 30, 80]
    for i, w in enumerate(base_w):
        p[f"MM_W{i}"] = max(5, w + rng.choice([-30, -15, 0, 0, 15, 30, 60]))
    return p
for i in range(ncfg):
    p = sample()
    env = dict(os.environ, **{k: str(v) for k, v in p.items()})
    r = subprocess.run(["python3", "v18-work/arena.py", "--bot", "./bot19", "--opp", f"{M}/{base}/bot", "--seeds", str(slo), str(nseeds), "--workers", "2", "--json"],
                       capture_output=True, text=True, env=env)
    try:
        o = json.loads(r.stdout)[f"{M}/{base}/bot"]
    except Exception as e:
        print("err", r.stdout[-200:], r.stderr[-200:]); continue
    rec = dict(base=base, params=p, v19_winrate=o["winrate"], win=o["win"], loss=o["loss"], forfeits=o["forfeits"], margin=o["avg_margin"])
    open(out, "a").write(json.dumps(rec) + "\n")
    print(i, o["winrate"], o["forfeits"], o["avg_margin"], flush=True)
