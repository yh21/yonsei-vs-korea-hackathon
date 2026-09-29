#!/bin/bash
cd /Users/yh/Desktop/yonsei-vs-korea-hackathon/v18-work/blotto
for cfg in "Q0=0 Q3=0 Q4=1 Q5=0" "Q0=0 Q3=0 Q4=0 Q5=1" "Q0=0 Q3=0.25 Q4=0 Q5=0" "Q0=1 Q2=1 Q3=0 Q4=0 Q5=0"; do
  echo "== $cfg"; tools/ev.sh "$cfg" 100 Y
done
