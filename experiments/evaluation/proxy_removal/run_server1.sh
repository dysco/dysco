#!/bin/sh

DYSCO=/home/ubuntu/dysco/sigcomm

killall -9 iperf
killall -9 dysco_daemon

DYSCO_MODULE=$DYSCO/nf/dysco.ko
sudo rmmod dysco
sudo insmod $DYSCO_MODULE

sleep 3

$DYSCO/user/dysco_daemon &

sleep 3

echo "start iperf server"

iperf -s > /dev/null
