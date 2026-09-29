#!/bin/bash
# usage: pool.sh BOT LO N OUTFILE [KEEPDIR]
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
BOT=$1; LO=$2; N=$3; OUT=$4; KEEP=$5
OPPS="./bot18 ./bot17 ./bot15 ./bot_opp ./v18-work/zoo/rusher/bot ./v18-work/zoo/turtle/bot ./v18-work/zoo/hunter/bot ./v18-work/zoo/blitz/bot ./v18-work/snap2/sim/bot ./v18-work/snap2/blotto/bot ./v18-work/snap2/tempo/bot"
K=""
[ -n "$KEEP" ] && K="--keep $KEEP"
python3 v18-work/arena.py --bot $BOT --opp $OPPS --seeds $LO $N --workers 2 $K > $OUT 2>&1
