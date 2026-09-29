#!/bin/bash
export DUEL_SRC=duel_q8
while ! grep -q TOTAL logs/suite_q6ord.log 2>/dev/null; do sleep 30; done
./suite.sh q8full SIM_STRIKE=4 SIM_STRIKE2=1 SIM_HEDGE=3 SIM_HEDGE_THR=8 SIM_H1=12 SIM_KEEP=5
./suite.sh q8nohedge SIM_STRIKE=4 SIM_STRIKE2=1 SIM_H1=12 SIM_KEEP=5
