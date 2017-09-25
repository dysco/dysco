#!/bin/sh -x

iperf_get_sport()
{
    #netstat -napt | grep iperf > LOG
    netstat -napt | grep 5001 > LOG
    SPORT=`cat LOG | tail -n 1 | awk '{print $4}' | cut -d: -f2`
    rm LOG
    echo $SPORT
}

#if [ $# -ne 7 ]; then
#    echo "$0 <# of threads in iperf>"
#    exit 1
#fi       

ulimit -n 65535

TIME=$1

DYSCO=/home/ubuntu/dysco/sigcomm

sudo killall -9 iperf
sudo killall -9 dysco_daemon

DYSCO_MODULE=$DYSCO/nf/dysco.ko
sudo rmmod dysco
sudo insmod $DYSCO_MODULE

sleep 3

$DYSCO/user/dysco_daemon > results/dysco_daemon_$6.log &

sleep 1

$DYSCO/user/dysco_ctl policy $2 2 $3 $4 $tcp dst port $5

sleep 5

echo "start iperf client"

#iperf -c $4 -p $5 -N -t $TIME -P $7 &
#$DYSCO/netperf_state/netperf_state send -addr $4:$5 -duration ${TIME}s -parallel $7 > results/netperf_state_$6.txt &

if [ $6 -eq 1 ]; then
    $DYSCO/netperf_state/netperf_state send -addr $4:$5 -duration ${TIME}s -parallel $7 > results/netperf_state_$6.txt &

    sleep $8

    #echo "start migration"
    #IPERF_SPORT=`iperf_get_sport`
    #$DYSCO/user/dysco_ctl rec_state $2 10.0.5.2 $4 $IPERF_SPORT 5001 10.0.3.2
    ## state migration is done by dysco_daemon (which would be implmented by callback)
    ##ssh 10.0.3.2 /home/ubuntu/eval_dysco/dysco_sigcomm17/bin/move_iptables -c 10.0.5.2
    
    sleep $8
    sleep 10
    
    $DYSCO/user/dysco_ctl get_rec_time 10.0.1.2 > results/rec_time.txt
else
    $DYSCO/netperf/netperf send -addr $4:$5 -duration ${TIME}s -parallel $7 > results/netperf_state_$6.txt &
fi
