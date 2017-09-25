#!/bin/sh

if [ $# -ne 7 ]; then
    echo "$0 <# of threads in netperf>"
    exit 1
fi       

ulimit -n 65535

TIME=$1

DYSCO=/home/ubuntu/dysco/sigcomm

sudo killall -9 netperf
sudo killall -9 dysco_daemon
sudo rmmod dysco

sleep 3

echo "start netperf client" $7:$4 " TIME " ${TIME}s " THREADS " $6

#iperf -f m -c $3 -p $4 -i 1 -t $TIME -P $6 > results/iperf_$5.txt &
#iperf -c $3 -p $4 -N -t $TIME -P $6 &
$DYSCO/netperf/netperf send -addr $7:$4 -duration ${TIME}s -parallel $6 &
