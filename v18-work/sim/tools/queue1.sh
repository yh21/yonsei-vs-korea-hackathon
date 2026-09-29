#!/bin/bash
export DUEL_SRC=duel_q1
while ! grep -q TOTAL logs/suite_stick2.log 2>/dev/null; do sleep 20; done
./suite.sh base0 SIM_STICK10=0 SIM_BASEB10=0
./suite.sh ek3 SIM_EK=3
./suite.sh h12 SIM_H=12
./suite.sh h30 SIM_H=30
./suite.sh strike4 SIM_STRIKE=4
./suite.sh rand8 SIM_RAND=8
