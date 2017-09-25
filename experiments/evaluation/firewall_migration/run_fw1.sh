#!/bin/sh

ulimit -n 65535

CLIENT=$1
SERVER=$2
OLD_FW=$3
NEW_FW=$4

DYSCO=/home/ubuntu/dysco/sigcomm

killall -9 dysco_daemon

DYSCO_MODULE=$DYSCO/nf/dysco.ko
sudo rmmod dysco
sudo insmod $DYSCO_MODULE

sleep 3

$DYSCO/user/dysco_daemon > results/dysco_daemon.log &

sleep 3

echo "start iptables 1"
./iptables.sh start
