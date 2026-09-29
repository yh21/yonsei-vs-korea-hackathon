#!/usr/bin/env python3
# convert a python replay json into a text file for tools/validate.cpp
import json, sys
d = json.load(open(sys.argv[1]))
out = open(sys.argv[2], "w")
m = d["map"]
out.write(f"SEED {d['seed']}\n")
out.write(f"BASES {m['bases']['Y'][0]} {m['bases']['Y'][1]} {m['bases']['K'][0]} {m['bases']['K'][1]}\n")
for row in m["terrain"]: out.write("T " + row + "\n")
out.write(f"B {len(m['buildings'])}\n")
for b in m["buildings"]: out.write(f"{b['id']} {b['x']} {b['y']} {b['type']} {b['score']}\n")
for t in d["turns"]:
    out.write(f"TURN {t['turn']}\n")
    for tm in "YK":
        out.write(f"C{tm} {len(t['commands'][tm])}\n")
        for l in t["commands"][tm]: out.write(l + "\n")
    s = t["state"]
    out.write(f"RES {s['resources']['Y']} {s['resources']['K']}\n")
    out.write(f"OCC {s['occupation']['Y']} {s['occupation']['K']}\n")
    out.write("OWN " + " ".join(("N" if b["owner"] == "N" else b["owner"]) + str(b["stage"]) for b in s["buildings"]) + "\n")
    out.write("REV " + " ".join(str(x) for x in s["revealed"]["Y"]) + " | " + " ".join(str(x) for x in s["revealed"]["K"]) + "\n")
    us = [u for u in s["units"] if u[1] != "S"]
    out.write(f"UNITS {len(us)}\n")
    for tm, k, x, y, c in us: out.write(f"{tm} {k} {x} {y} {c}\n")
out.write("EOF\n")
