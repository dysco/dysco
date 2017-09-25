#!/bin/sh

K=$1

for f in `ls result_K${K}*.txt`
do
    echo "$f", `cat $f | head -n 1000 | awk '{m+=$1} END{print m/NR;}'`
done
