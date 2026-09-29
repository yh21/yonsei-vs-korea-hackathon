#!/bin/bash
# policy-only A/B (no search): runpol.sh NAME "ENVS" [opps...]
name=$1; envs=$2; shift 2
opps=${@:-"./bot17"}
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
env P43=0 $envs python3 v18-work/stack/arena3.py --bot ./v18-work/stack/bot_a9 --opp $opps --seeds 10000 40 --sides Y --workers 3 --tmo 3000 > v18-work/stack/logs/pol_$name.txt 2>&1
