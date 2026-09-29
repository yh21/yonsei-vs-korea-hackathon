#!/bin/bash
# usage: ex.sh "P0=8 P1=7" [opps...]   (seeds 10000..10199)
cd "$(dirname "$0")"
E="$1"; shift
OPPS=${@:-"15 16"}
echo "== $E"
env $E ./arena --a t --opps $OPPS --seeds ${LO:-10000} ${N:-200} --jobs ${J:-6} | grep -v lost
