#!/bin/bash
# usage: oldsuite.sh LABEL [ENV=VAL ...]   mine vs the older bots v5..v14 (structurally different code), both sides
LABEL=$1; shift
export DUEL_SRC=${DUEL_SRC:-duel_all}
export SIM_MS=100000 SIM_STRIKE=${SIM_STRIKE:-0}
for kv in "$@"; do export "$kv"; done
OUT=logs/old_$LABEL.log
: > $OUT
for i in 5 6 7 8 9 10 11 12 13 14; do python3 batch.py mine v$i --seeds 10000 5 --jobs 3 --sides YK >> $OUT 2>&1; done
python3 - <<PY >> $OUT
import re
w=l=d=0
for line in open("$OUT"):
    m=re.search(r"(\d+)W (\d+)D (\d+)L",line)
    if m: w+=int(m.group(1)); d+=int(m.group(2)); l+=int(m.group(3))
print(f"TOTAL {w}W {d}D {l}L winrate={(w+0.5*d)/max(1,w+d+l):.3f}")
PY
