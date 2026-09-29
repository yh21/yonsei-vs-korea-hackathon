#!/bin/bash
export DUEL_SRC=duel_q2
while ! grep -q TOTAL logs/suite_rand8.log 2>/dev/null; do sleep 30; done
./suite.sh earlyb SIM_EARLYB10=40
./suite.sh earlyb_stick4 SIM_EARLYB10=40 SIM_STICK10=40
