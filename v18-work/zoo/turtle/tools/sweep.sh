#!/bin/bash
# usage: sweep.sh name "P9=50 P11=5" seeds
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
name=$1; envs=$2; n=${3:-15}
env $envs python3 v18-work/arena.py --bot ./v18-work/zoo/turtle/bot_tune --opp ./bot8 ./bot13 --seeds 10000 $n --workers 2 --json > v18-work/zoo/turtle/sw_$name.json
python3 - <<PY
import json
d=json.load(open('v18-work/zoo/turtle/sw_$name.json'))
print('$name','$envs',' '.join('%s:%d/%d ff%d m%.1f'%(k[2:],o['win'],o['games'],o['forfeits'],o['avg_margin']) for k,o in d.items()))
PY
