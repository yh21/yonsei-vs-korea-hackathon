#!/bin/bash
# usage: tools/ev2.sh DUELBIN "cfg env" seeds side opps...
bin="$1"; cfg="$2"; n=${3:-200}; side=${4:-Y}; shift 4
opps=${@:-"v17 v16 v15"}
for o in $opps; do
  env $cfg $bin --me blotto --opp $o --seeds 10000 $n --threads 6 --side $side | tr '\n' ' ' | sed 's/lost:/ lost:/'; echo
done
