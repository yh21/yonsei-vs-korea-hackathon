#!/bin/bash
# usage: runab.sh NAME "ENV=.. ENV=.." BOT OPPS SEEDLO N [extra arena args]
name=$1; envs=$2; bot=$3; opps=$4; lo=$5; n=$6; shift 6
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
env $envs python3 v18-work/stack/arena3.py --bot $bot --opp $opps --seeds $lo $n --sides Y --workers 4 --tmo 8000 "$@" > v18-work/stack/logs/$name.txt 2>&1
