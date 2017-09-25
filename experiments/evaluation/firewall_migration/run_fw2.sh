#!/bin/sh

DYSCO=/home/ubuntu/dysco/sigcomm

killall -9 dysco_daemon

DYSCO_MODULE=$DYSCO/nf/dysco.ko
sudo rmmod dysco
sudo insmod $DYSCO_MODULE

sleep 3

$DYSCO/user/dysco_daemon > results/dysco_daemon.log &

sleep 3

echo "start iptables 2"
./iptables.sh start
sudo sh -c 'echo 1 > /proc/sys/net/netfilter/nf_conntrack_tcp_be_liberal'

# state migration is done by dysco_daemon (which would be implmented by callback)
#../../bin/move_iptables -s &
