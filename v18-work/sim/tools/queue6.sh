#!/bin/bash
export DUEL_SRC=duel_q6
while ! grep -q TOTAL logs/suite_q5v2two.log 2>/dev/null; do sleep 30; done
./suite.sh q6s2 SIM_STRIKE=4 SIM_STRIKE2=1
./suite.sh q6ord SIM_STRIKE=4 SIM_ORDER=1 SIM_VAR=0
