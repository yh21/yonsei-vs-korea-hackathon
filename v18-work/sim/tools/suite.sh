#!/bin/bash
# usage: suite.sh LABEL [ENV=VAL ...]   runs the standard dev suite (mirror-symmetric => Y side only) in the background-friendly way
LABEL=$1; shift
export DUEL_SRC=${DUEL_SRC:-duel}
export SIM_MS=100000 SIM_STRIKE=${SIM_STRIKE:-0}
for kv in "$@"; do export "$kv"; done
OUT=logs/suite_$LABEL.log
: > $OUT
python3 batch.py mine v15 --seeds 10000 30 --jobs 3 --sides Y --quiet >> $OUT 2>&1
python3 batch.py mine v17 --seeds 10030 20 --jobs 3 --sides Y --quiet >> $OUT 2>&1
for k in 1 2 3 4 5 6 7 8; do python3 batch.py mine pp:$k --seeds 10000 10 --jobs 3 --sides Y --quiet >> $OUT 2>&1; done
python3 - <<PY >> $OUT
import re
w=l=d=0
for line in open("$OUT"):
    m=re.search(r"(\d+)W (\d+)D (\d+)L",line)
    if m: w+=int(m.group(1)); d+=int(m.group(2)); l+=int(m.group(3))
print(f"TOTAL {w}W {d}D {l}L winrate={(w+0.5*d)/max(1,w+d+l):.3f}")
PY
