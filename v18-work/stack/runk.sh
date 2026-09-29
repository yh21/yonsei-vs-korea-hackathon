#!/bin/bash
name=$1; envs=$2; bot=$3; lo=$4; n=$5; shift 5
opps=${@:-"./bot17 ./bot16 ./bot15 ./bot_opp"}
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
env P44=1000000 $envs python3 v18-work/stack/arena3.py --bot $bot --opp $opps --seeds $lo $n --sides K --workers 4 --tmo 8000 --keep v18-work/stack/rp_$name > v18-work/stack/logs/full_$name.txt 2>&1
