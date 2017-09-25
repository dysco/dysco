#!/bin/bash

source config.sh

DIR=/home/ubuntu/eval_dysco/dysco_sigcomm17/evaluation/proxy_removal/results

ssh $SERVER1 mkdir $DIR/old
ssh $SERVER1 mv $DIR/iperf_1.txt $DIR/old
ssh $SERVER2 mkdir $DIR/old
ssh $SERVER2 mv $DIR/iperf_2.txt $DIR/old
ssh $SERVER3 mkdir $DIR/old
ssh $SERVER3 mv $DIR/iperf_3.txt $DIR/old
ssh $SERVER4 mkdir $DIR/old
ssh $SERVER4 mv $DIR/iperf_4.txt $DIR/old
ssh $PROXY mkdir $DIR/old
ssh $PROXY mv $DIR/cpu_proxy_*.txt $DIR/old

scp $PROXY:$DIR/old/cpu_proxy_*.txt results/
scp $SERVER1:$DIR/old/iperf_1.txt results/
scp $SERVER2:$DIR/old/iperf_2.txt results/
scp $SERVER3:$DIR/old/iperf_3.txt results/
scp $SERVER4:$DIR/old/iperf_4.txt results/
scp $PROXY:$DIR/old/cpu_proxy_*.txt results/

for i in `seq 1 4`; do
    cat results/iperf_1.txt | grep SUM | wc
done
