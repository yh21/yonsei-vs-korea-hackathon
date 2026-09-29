#!/bin/bash
# usage: selfblot.sh NAME "ENV" [n]   -> bot_a29c vs cand_blotto
name=$1; env_=$2; n=${3:-20}
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
P44=1000000 python3 v18-work/stack/arena3.py --bot "env $env_ ./v18-work/stack/bot_a29c" --opp ./v18-work/stack/zoo3/cand_blotto --seeds 10000 $n --sides YK --workers 3 --tmo 8000 --keep v18-work/stack/rp_br_$name > v18-work/stack/logs/br_$name.txt 2>&1
