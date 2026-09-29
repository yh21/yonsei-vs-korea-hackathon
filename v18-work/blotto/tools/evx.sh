#!/bin/bash
# usage: tools/evx.sh DUELBIN "cfg env" seeds side opps(names or paths)
bin="$1"; cfg="$2"; n=${3:-100}; side=${4:-Y}; shift 4
for o in "$@"; do
  env $cfg $bin --me blotto --opp $o --seeds 10000 $n --threads 6 --side $side | tr '\n' ' ' | sed 's/lost:/ lost:/'; echo
done
