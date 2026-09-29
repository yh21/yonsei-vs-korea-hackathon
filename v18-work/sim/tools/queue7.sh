#!/bin/bash
export DUEL_SRC=duel_q6
while ! grep -q TOTAL logs/suite_q5v2s4.log 2>/dev/null; do sleep 30; done
./oldsuite.sh q6base SIM_STRIKE=4
