#!/bin/sh -x

if [ $# -ne 5 ]; then
    echo "$0 <# of threads in iperf>"
    exit 1
fi       

ulimit -n 65535

WAIT=$1

DYSCO=/home/ubuntu/dysco/sigcomm

#sudo killall -9 tcp_proxy
#sudo killall -9 dysco_daemon
#
#DYSCO_MODULE=$DYSCO/nf/dysco.ko
sudo rmmod dysco
sleep 2
sudo insmod $DYSCO_MODULE
#
#sleep 3
#
$DYSCO/user/dysco_daemon &
#
#sleep 1

#$DYSCO/user/dysco_ctl policy $2 1 $3 tcp dst port $4

#sleep 3

echo "start tcp_proxy server"

#SHIFT=0x00000003
#if [ $5 -eq 2 ]; then
#    SHIFT=0x0000000C
#elif [ $5 -eq 3 ]; then
#    SHIFT=0x00000030
#elif [ $5 -eq 4 ]; then
#    SHIFT=0x000000C0
#fi
#taskset $SHIFT $DYSCO/user/tcp_proxy $4 $WAIT $3 &
#$DYSCO/user/tcp_proxy $4 $WAIT $3 &

#pidstat -h -r -u -v -p $! 1 > results/cpu_proxy_$5.txt &
