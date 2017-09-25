#!/bin/sh

if [ $# -ne 6 ]; then
    echo "$0 <# of threads in iperf>"
    exit 1
fi       

ulimit -n 65535

TIME=$1

DYSCO=/home/ubuntu/dysco/sigcomm

sudo killall -9 iperf
sudo killall -9 netperf
sudo killall -9 dysco_daemon

DYSCO_MODULE=$DYSCO/nf/dysco.ko
sudo rmmod dysco
sudo insmod $DYSCO_MODULE

sleep 3

$DYSCO/user/dysco_daemon &

sleep 1

$DYSCO/user/dysco_ctl policy $2 1 $3 tcp dst port $4

sleep 3

echo "start iperf client"

#iperf -f m -c $3 -p $4 -i 1 -t $TIME -P $6 > results/iperf_$5.txt &
#iperf -c $3 -p $4 -N -t $TIME -P $6 &
$DYSCO/netperf/netperf send -addr $3:$4 -duration ${TIME}s -parallel $6 &
