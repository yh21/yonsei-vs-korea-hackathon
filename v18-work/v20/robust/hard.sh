#!/bin/bash
# usage: hard.sh BOT LO N OUTFILE [opps...]  (per-opponent, appends)
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
BOT=$1; LO=$2; N=$3; OUT=$4; shift 4
OPPS="$@"
[ -z "$OPPS" ] && OPPS="./v18-work/snap2/tempo/bot ./bot18 ./v18-work/snap2/blotto/bot ./v18-work/snap2/sim/bot ./v18-work/zoo/blitz/bot ./v18-work/zoo/rusher/bot"
: > $OUT
for o in $OPPS; do
  python3 v18-work/arena.py --bot $BOT --opp $o --seeds $LO $N --workers 2 >> $OUT 2>&1
done
echo DONE >> $OUT
