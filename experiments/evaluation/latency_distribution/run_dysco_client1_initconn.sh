#!/bin/bash

ulimit -n 65535

sudo killall -9 dysco_daemon

sleep 1

source config.sh

sudo rmmod dysco
sudo insmod $DYSCO_MODULE

sleep 1

../../user/dysco_daemon &

sleep 1

../../user/dysco_ctl policy $CLIENT_IP1 2 $MIDDLE_IP1 $SERVER_IP1 tcp dst port $SERVER_PORT1

sudo ip route add $SERVER_IP1/32 via $MIDDLE_IP1 dev $IF1

CMD=../../bin/init_connections_client
$CMD $SERVER_IP1 $SERVER_PORT1 $1
