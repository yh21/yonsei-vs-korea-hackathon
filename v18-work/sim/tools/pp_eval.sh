#!/bin/bash
# usage: pp_eval.sh BOT SEEDS_LO SEEDS_N JOBS PPLIST... ; prints per-pp results
BOT=$1; LO=$2; N=$3; J=$4; shift 4
for k in "$@"; do python3 batch.py $BOT pp:$k --seeds $LO $N --jobs $J --quiet; done
