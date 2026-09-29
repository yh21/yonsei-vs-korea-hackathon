#!/bin/bash
# usage: sw.sh "P30=1" "P30=3" ...   -> one summary line per env config
cd "$(dirname "$0")"
for E in "$@"; do
  LINE=""
  while read -r l; do
    o=$(echo "$l" | sed -E 's/^t vs ([^:]+):.*/\1/')
    wr=$(echo "$l" | sed -E 's/.*winrate=([0-9.]+).*/\1/')
    mg=$(echo "$l" | sed -E 's/.*margin=([-0-9.]+).*/\1/')
    LINE="$LINE ${o##*/}:$wr($mg)"
  done < <(env $E ./arena --a t --opps ${OPPS:-15 16 17} --seeds ${LO:-10000} ${N:-200} --jobs ${J:-6} | grep "winrate")
  echo "$E =>$LINE"
done
