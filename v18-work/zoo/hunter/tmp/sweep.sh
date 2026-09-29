#!/bin/bash
# usage: sweep.sh "P10=0.6 P11=3" [seeds]
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
env $1 python3 v18-work/arena.py --bot ./v18-work/zoo/hunter/bot_tune --opp ./bot8 --seeds 10000 ${2:-8} --workers 2 | head -1
