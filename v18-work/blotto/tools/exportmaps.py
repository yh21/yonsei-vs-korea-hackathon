#!/usr/bin/env python3
# export maps for seeds lo..hi-1 (dev seeds only!) to a text file for the C++ harness
import sys, os
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
sys.path.insert(0, os.path.join(ROOT, "yk-development-tools"))
from engine.config import load_config
from mapgen import generate
lo, hi, out = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
assert lo >= 10000 and hi <= 10200, "dev seeds only"
cfg = load_config()
with open(out, "w") as f:
    for seed in range(lo, hi):
        g = generate(seed, cfg)
        f.write(f"SEED {seed}\n")
        f.write(f"BASES {g.bases['Y'][0]} {g.bases['Y'][1]} {g.bases['K'][0]} {g.bases['K'][1]}\n")
        for row in g.terrain: f.write("T " + "".join(row) + "\n")
        f.write(f"B {len(g.buildings)}\n")
        for b in g.buildings: f.write(f"{b.id} {b.x} {b.y} {b.btype} {b.score}\n")
