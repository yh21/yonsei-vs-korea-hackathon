#!/usr/bin/env python3
"""Build duel harness: compiles every bot in bots.txt twice (A=Y side, B=K side) with renamed decide/main, then links duel."""
import os, subprocess, sys, hashlib
from concurrent.futures import ThreadPoolExecutor
here = os.path.dirname(os.path.abspath(__file__)); os.chdir(here)
os.makedirs("obj", exist_ok=True)
bots = []
for line in open("bots.txt"):
    line = line.strip()
    if not line or line.startswith("#"): continue
    parts = line.split()
    bots.append((parts[0], parts[1], parts[2:]))
jobs = []
for name, src, flags in bots:
    src = os.path.abspath(src)
    for side in "AB":
        obj = f"obj/{name}{side}.o"
        d = os.path.dirname(src)
        blob = open(src, "rb").read() + " ".join(flags).encode()
        for h in sorted(os.listdir(d)):
            if h.endswith(".hpp"): blob += open(os.path.join(d, h), "rb").read()
        key = hashlib.md5(blob).hexdigest()
        stamp = obj + ".key"
        if os.path.exists(obj) and os.path.exists(stamp) and open(stamp).read() == key: continue
        cmd = ["g++", "-std=c++20", "-O2", "-c", f"-Ddecide=decide_{name}{side}", f"-Dmain=main_{name}{side}", "-w", *flags, src, "-o", obj]
        jobs.append((cmd, stamp, key))
def run(j):
    cmd, stamp, key = j
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: print(" ".join(cmd)); print(r.stderr[-3000:]); sys.exit(1)
    open(stamp, "w").write(key)
with ThreadPoolExecutor(4) as ex: list(ex.map(run, jobs))
with open("registry.inc", "w") as f:
    for name, _, _ in bots:
        for side in "AB": f.write(f"vector<string> decide_{name}{side}(const p::View&, const p::Init&);\n")
    f.write("struct Entry { const char* name; DecideFn A, B; };\nstatic Entry registry[] = {\n")
    for name, _, _ in bots: f.write(f'  {{"{name}", decide_{name}A, decide_{name}B}},\n')
    f.write("};\n")
objs = [f"obj/{n}{s}.o" for n, _, _ in bots for s in "AB"]
r = subprocess.run(["g++", "-std=c++20", "-O2", "-w", "duel.cpp", *objs, "-o", (sys.argv[1] if len(sys.argv) > 1 else "duel")], capture_output=True, text=True)
if r.returncode: print(r.stderr[-3000:]); sys.exit(1)
print("built", [b[0] for b in bots])
