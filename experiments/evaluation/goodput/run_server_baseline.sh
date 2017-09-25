#!/bin/sh

ulimit -n 65535

DYSCO=/home/ubuntu/dysco/sigcomm

#sudo killall -9 netperf
sudo killall -9 dysco_daemon

echo "start netperf server"
$DYSCO/netperf/netperf receive -addr 0.0.0.0:$1 > results/iperf_$2.txt &
