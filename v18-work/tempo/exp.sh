#!/bin/bash
# usage: exp.sh NAME BIN "ENV1=.. ENV2=.." [opps] [lo n]
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon
NAME=$1; BIN=$2; ENVS=$3; OPPS=${4:-"./bot15 ./bot16"}; LO=${5:-10000}; N=${6:-100}
python3 v18-work/tempo/tarena.py --bot $BIN --opp $OPPS --seeds $LO $N --workers 3 --env $ENVS > v18-work/tempo/res_$NAME.txt 2>&1
