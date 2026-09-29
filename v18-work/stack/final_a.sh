#!/bin/bash
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
B=./v18-work/stack/bot_a28c
P44=1000000 python3 v18-work/stack/arena3.py --bot $B --opp ./v18-work/stack/zoo3/cand_blotto --seeds 10000 20 --sides YK --workers 3 --tmo 8000 --keep v18-work/stack/rp_fin_blotto > v18-work/stack/logs/fin_blotto.txt 2>&1
P44=1000000 python3 v18-work/stack/arena3.py --bot $B --opp ./v18-work/stack/zoo3/blitz ./v18-work/stack/zoo3/hunter ./v18-work/stack/zoo3/rusher ./v18-work/stack/zoo3/turtle ./v18-work/stack/zoo3/cand_sim ./v18-work/stack/zoo3/cand_tempo --seeds 10000 10 --sides YK --workers 3 --tmo 8000 --keep v18-work/stack/rp_fin_zoo > v18-work/stack/logs/fin_zoo.txt 2>&1
