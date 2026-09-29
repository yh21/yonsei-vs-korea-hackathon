#!/bin/bash
# usage: tools/ev.sh "Q0=0 Q3=0" [seeds_n] [side] [opps...]
cfg="$1"; n=${2:-200}; side=${3:-Y}; shift 3
opps=${@:-"v17 v16 v15"}
for o in $opps; do
  env $cfg ./harness/duel --me blotto --opp $o --seeds 10000 $n --threads 6 --side $side | head -2 | tr '\n' ' ' | sed 's/lost:.*//' ; echo
done
