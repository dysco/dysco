#!/bin/bash

source config.sh

DIR=/home/ubuntu/eval_dysco/dysco_sigcomm17/evaluation/firewall_migration/results

ssh $SERVER1 mkdir $DIR/old
ssh $SERVER1 mv $DIR/iperf_1.txt $DIR/old
ssh $SERVER2 mkdir $DIR/old
ssh $SERVER2 mv $DIR/iperf_2.txt $DIR/old
ssh $SERVER3 mkdir $DIR/old
ssh $SERVER3 mv $DIR/iperf_3.txt $DIR/old

scp $SERVER1:$DIR/old/iperf_1.txt results/
scp $SERVER2:$DIR/old/iperf_2.txt results/
scp $SERVER3:$DIR/old/iperf_3.txt results/

cat results/iperf_1.txt | wc
cat results/iperf_2.txt | wc
cat results/iperf_3.txt | wc
