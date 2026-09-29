#!/bin/bash
# usage: selfab.sh NAME "ENV_A" "ENV_B" [seedLo] [n]   (A = bot_a15c under test, B = bot_a15d baseline)
name=$1; ea=$2; eb=$3; lo=${4:-10000}; n=${5:-20}
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
P44=1000000 python3 v18-work/stack/arena3.py --bot "env $ea ./v18-work/stack/bot_a15c" --opp "env $eb ./v18-work/stack/bot_a15d" --seeds $lo $n --sides YK --workers 3 --tmo 8000 > v18-work/stack/logs/sab_$name.txt 2>&1
