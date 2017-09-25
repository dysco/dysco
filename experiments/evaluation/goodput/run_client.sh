#!/bin/sh

if [ $# -ne 7 ]; then
    echo "$0 <# of threads in netperf>"
    exit 1
fi       

ulimit -n 65535

TIME=$1

DYSCO=/home/ubuntu/dysco/sigcomm

#sudo killall -9 netperf
sudo killall -9 dysco_daemon

DYSCO_MODULE=$DYSCO/nf/dysco.ko
sudo rmmod dysco
sudo insmod $DYSCO_MODULE

sleep 3

$DYSCO/user/dysco_daemon > results/dysco_daemon.log &

sleep 1

$DYSCO/user/dysco_ctl policy $2 2 $3 $7 tcp dst port $4

sleep 3

echo "start netperf client" $7:$4 " TIME " ${TIME}s " THREADS " $6

#iperf -f m -c $3 -p $4 -i 1 -t $TIME -P $6 > results/iperf_$5.txt &
#iperf -c $3 -p $4 -N -t $TIME -P $6 &
echo $DYSCO/netperf/netperf send -addr $7:$4 -duration ${TIME}s -parallel $6
$DYSCO/netperf/netperf send -addr $7:$4 -duration ${TIME}s -parallel $6 &

