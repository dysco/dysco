#!/bin/bash

for n in 1 2 3
do

    # DYSCO
    for i in 1 25 250 2500
    do
        ./autorun_dysco.sh $i
        sleep 300
        num=`expr 4 \* $i`
        #num=`expr 1 \* $i`
        ./get_results.sh
        DIR=results/dysco_${num}_${n}
        mkdir $DIR
        cp results/iperf_*.txt $DIR
        sleep 10
    done

    # baseline
    for i in 1 25 250 2500
    do
        ./autorun_baseline.sh $i
        sleep 300
        num=`expr 4 \* $i`
        #num=`expr 1 \* $i`
        ./get_results.sh
        DIR=results/baseline_${num}_${n}
        mkdir $DIR
        cp results/iperf_*.txt $DIR
        sleep 10
    done

done
