#!/bin/bash
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
B=./v18-work/stack/bot_a28c
P44=1000000 python3 v18-work/stack/arena3.py --bot $B --opp ./bot17 ./bot16 ./bot15 ./bot_opp --seeds 10080 40 --sides Y --workers 3 --tmo 8000 --keep v18-work/stack/rp_fin_lineage > v18-work/stack/logs/fin_lineage.txt 2>&1
P44=1000000 python3 v18-work/stack/arena3.py --bot $B --opp ./bot17 ./bot16 ./bot15 ./bot_opp --seeds 10120 20 --sides K --workers 3 --tmo 8000 --keep v18-work/stack/rp_fin_lineageK > v18-work/stack/logs/fin_lineageK.txt 2>&1
