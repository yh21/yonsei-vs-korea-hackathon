#!/bin/bash
# usage: runpanel.sh NAME "ENVS" BOT [seedsLo] [N]
name=$1; envs=$2; bot=${3:-./v18-work/stack/bot_a7c}; lo=${4:-10000}; n=${5:-12}
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
env P44=1000000 $envs python3 v18-work/stack/arena3.py --bot $bot --opp ./bot17 ./bot16 ./v18-work/stack/zoo3/blitz ./v18-work/stack/zoo3/rusher --seeds $lo $n --sides Y --workers 4 --tmo 8000 --keep v18-work/stack/rp_$name > v18-work/stack/logs/panel_$name.txt 2>&1
