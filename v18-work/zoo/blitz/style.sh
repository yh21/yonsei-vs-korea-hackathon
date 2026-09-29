#!/bin/bash
# usage: style.sh "ENV=.." BOT OPP  -> runs 8 seeds x 2 sides, prints TELE usage stats
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
for kv in $1; do export "$kv"; done
BOT=$2; OPP=$3
rm -rf v18-work/zoo/blitz/replays/style; mkdir -p v18-work/zoo/blitz/replays/style
for s in 10000 10001 10002 10003 10004 10005 10006 10007; do
python3 yk-development-tools/bots/dist/starter/play_local.py --bot $BOT --opponent $OPP --seed $s --replay v18-work/zoo/blitz/replays/style/y_$s.json >/dev/null
python3 yk-development-tools/bots/dist/starter/play_local.py --bot $OPP --opponent $BOT --seed $s --replay v18-work/zoo/blitz/replays/style/k_$s.json >/dev/null
done
python3 - <<'PY'
import json,glob
files=sorted(glob.glob('v18-work/zoo/blitz/replays/style/*.json'))
tw=tf=0
for f in files:
    d=json.load(open(f)); me='Y' if f.split('/')[-1].startswith('y_') else 'K'
    for t in d['turns']:
        for c in t['commands'][me]:
            if c.startswith('TELE'):
                if c.split()[3]=='W': tw+=1
                else: tf+=1
print('games',len(files),'TELE W/game %.1f'%(tw/len(files)),'TELE F/game %.1f'%(tf/len(files)))
PY
