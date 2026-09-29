#!/bin/bash
# usage: ev2.sh "P1=2 P3=4" BOT "opp list" seeds_lo n
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
for kv in $1; do export "$kv"; done
BOT=$2; OPPS=$3; LO=${4:-10000}; N=${5:-20}
python3 v18-work/arena.py --bot $BOT --opp $OPPS --seeds $LO $N --workers 2 2>&1 | grep -v lost | sed 's/\.\/v18-work\/zoo\/blitz\///' | awk '{print $1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14}'
