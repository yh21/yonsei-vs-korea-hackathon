#!/bin/bash
export DUEL_SRC=duel_q5
while pgrep -f "suite.sh q3strike4" > /dev/null; do sleep 20; done
./suite.sh q5v2
./suite.sh q5v2s4 SIM_STRIKE=4
./suite.sh q5v2h3 SIM_HEDGE=3
./suite.sh q5v2two SIM_H1=10 SIM_KEEP=4 SIM_H=24
