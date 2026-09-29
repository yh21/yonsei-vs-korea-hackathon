#!/bin/bash
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
M=./v18-work/zoo2/mimic
run(){ echo "== $*"; env "$@" python3 v18-work/arena.py --bot $M/patch/bot_tune --opp $M/teleraid/bot --seeds 12000 30 --workers 2 | grep -v lost | sed 's/.*bot vs //' ; }
run P94=100 P95=8
run P64=7
run P94=100 P95=8 P64=7
run P63=2
run P66=6 P94=100
run P64=8 P63=3 P94=100 P95=8
