#!/bin/bash
# lineage regression for a build: reg.sh NAME BOT "ENV" [lo] [n]
name=$1; bot=$2; envs=$3; lo=${4:-10000}; n=${5:-20}
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
env P44=1000000 $envs python3 v18-work/stack/arena3.py --bot ./v18-work/stack/$bot --opp ./bot17 ./bot16 ./bot15 ./bot_opp --seeds $lo $n --sides Y --workers 3 --tmo 8000 --keep v18-work/stack/rp_reg_$name > v18-work/stack/logs/reg_$name.txt 2>&1
