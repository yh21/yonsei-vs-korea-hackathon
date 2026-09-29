#!/bin/bash
export DUEL_SRC=duel_q3
./suite.sh q3base
./suite.sh q3strike4 SIM_STRIKE=4
./suite.sh q3two SIM_H1=10 SIM_KEEP=4 SIM_H=24
./suite.sh q3hedge SIM_HEDGE=1 SIM_HEDGE_THR=25
./suite.sh q3ek3 SIM_EK=3
