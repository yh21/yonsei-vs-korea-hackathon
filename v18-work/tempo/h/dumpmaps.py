#!/usr/bin/env python3
"""시드 범위의 맵을 텍스트로 덤프(C++ 하네스용). python3 dumpmaps.py LO N OUT"""
import sys, os
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
sys.path.insert(0, os.path.join(ROOT, "yk-development-tools"))
from engine.config import load_config
from mapgen import generate
lo, n, out = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
cfg = load_config()
with open(out, "w") as f:
    for s in range(lo, lo + n):
        g = generate(s, cfg)
        f.write(f"SEED {s}\n")
        for row in g.terrain:
            f.write("".join(row) + "\n")
        f.write(f"BASE {g.bases['Y'][0]} {g.bases['Y'][1]} {g.bases['K'][0]} {g.bases['K'][1]}\n")
        f.write(f"NB {len(g.buildings)}\n")
        for b in g.buildings:
            f.write(f"{b.id} {b.x} {b.y} {b.btype} {b.score}\n")
