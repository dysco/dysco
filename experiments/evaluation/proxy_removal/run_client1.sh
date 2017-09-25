#!/bin/sh

if [ $# -ne 1 ]; then
    echo "$0 <# of threads in iperf>"
    exit 1
fi       

DYSCO=/home/ubuntu/dysco/sigcomm

killall -9 iperf
killall -9 dysco_daemon

DYSCO_MODULE=$DYSCO/nf/dysco.ko
sudo rmmod dysco
sudo insmod $DYSCO_MODULE

sleep 3

$DYSCO/user/dysco_daemon &

sleep 3

$DYSCO/user/dysco_ctl policy 10.0.1.2 1 10.0.3.2 tcp dst port 5001

sleep 5

echo "start iperf client"

iperf -f m -c 10.0.3.2 -i 1 -t 60 -P $1 > results/iperf_$1.txt
