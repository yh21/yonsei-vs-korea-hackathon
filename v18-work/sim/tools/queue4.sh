#!/bin/bash
export DUEL_SRC=duel_q4
while ! grep -q TOTAL logs/suite_q3ek3.log 2>/dev/null; do sleep 30; done
./suite.sh q4hedge2 SIM_HEDGE=2
./suite.sh q4hedge3 SIM_HEDGE=3
