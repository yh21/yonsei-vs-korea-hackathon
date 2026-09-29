#!/bin/bash
# usage: selfab2.sh NAME BOT_A "ENV_A" BOT_B "ENV_B" [seedLo] [n]
name=$1; ba=$2; ea=$3; bb=$4; eb=$5; lo=${6:-10000}; n=${7:-16}
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
P44=1000000 python3 v18-work/stack/arena3.py --bot "env $ea ./v18-work/stack/$ba" --opp "env $eb ./v18-work/stack/$bb" --seeds $lo $n --sides YK --workers 3 --tmo 8000 > v18-work/stack/logs/sab_$name.txt 2>&1
