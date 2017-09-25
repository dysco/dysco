#!/bin/bash

ulimit -n 65535

source config.sh

sudo killall -9 dysco_daemon

sleep 1

sudo rmmod dysco
sudo insmod $DYSCO_MODULE

sleep 1

../../user/dysco_daemon &

sleep 1

../../user/dysco_ctl policy $CLIENT_IP 2 $MIDDLE_IP1 $SERVER_IP tcp dst port $SERVER_PORT

sudo ip route add $SERVER_IP/32 via $MIDDLE_IP1 dev $IF1

sleep 5

CMD=../../bin/client
$CMD $SERVER_IP $SERVER_PORT 1000
