#!/bin/bash
# usage: build.sh [extra flags for tempo bot e.g. -DTUNE]
set -e
cd "$(dirname "$0")"
R=../../..
FL="-std=c++20 -O2"
g++ $FL -c -Ddecide=decide17 -Dmain=main17 $R/yjc-submission17-cpp/main.cpp -o bot17.o 2>/dev/null &
g++ $FL -c -Ddecide=decide16 -Dmain=main16 $R/yjc-submission16-cpp/main.cpp -o bot16.o 2>/dev/null &
g++ $FL -c -Ddecide=decide15 -Dmain=main15 $R/yjc-submission15-cpp/main.cpp -o bot15.o 2>/dev/null &
g++ $FL $@ -c -Ddecide=decideT -Dmain=mainT ../main.cpp -o botT.o 2>/dev/null &
g++ $FL -c arena.cpp -o arena.o &
wait
g++ $FL -o arena arena.o botT.o bot17.o bot16.o bot15.o
echo built
