#!/bin/sh

ulimit -n 65535

DYSCO=/home/ubuntu/dysco/sigcomm

sudo killall -9 iperf
sudo killall -9 netperf
sudo killall -9 dysco_daemon

DYSCO_MODULE=$DYSCO/nf/dysco.ko
sudo rmmod dysco
sudo insmod $DYSCO_MODULE

sleep 3

$DYSCO/user/dysco_daemon > results/dysco_daemon.log &

sleep 3

echo "start iperf server"

#iperf -s -l 10M -f m -N -i 1 -p $1 -D > results/iperf_$2.txt
$DYSCO/netperf/netperf receive -addr 0.0.0.0:$1 > results/iperf_$2.txt &
