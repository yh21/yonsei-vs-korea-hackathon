#!/bin/bash
# usage: ev.sh "P13=14 P23=3" [lo] [n] [opps...]
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
ENVS="$1"; LO=${2:-10000}; N=${3:-10}; shift 3
OPPS=${@:-"./bot8 ./bot13"}
env $ENVS python3 v18-work/arena.py --bot ./v18-work/zoo/rusher/bot_t --opp $OPPS --seeds $LO $N --workers 3 | grep -v lost
