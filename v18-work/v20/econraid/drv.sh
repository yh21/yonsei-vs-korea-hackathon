#!/bin/bash
# usage: drv.sh TAG BOT LO N opp1 opp2 ...   (one arena call per opponent, result json per opponent)
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
tag=$1; bot=$2; lo=$3; n=$4; shift 4
for opp in "$@"; do
  name=$(basename $(dirname $opp)); [ "$name" = "." ] && name=$(basename $opp); [ "$name" = "zoo" ] && name=$(basename $opp)
  python3 v18-work/arena.py --bot $bot --opp $opp --seeds $lo $n --workers 1 --json > v18-work/v20/econraid/res/${tag}__${name}.json 2>&1
done
