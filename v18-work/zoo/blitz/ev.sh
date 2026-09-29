#!/bin/bash
# usage: ev.sh BOT "opp list" seeds_lo n  (env passthrough)
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
BOT=$1; OPPS=$2; LO=${3:-10000}; N=${4:-20}
python3 v18-work/arena.py --bot $BOT --opp $OPPS --seeds $LO $N --workers 2 2>&1 | grep -v lost | sed 's/\.\/v18-work\/zoo\/blitz\///' | awk '{print $1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14}'
