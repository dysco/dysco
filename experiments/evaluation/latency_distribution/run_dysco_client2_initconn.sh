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

../../user/dysco_ctl policy $CLIENT_IP2 2 $MIDDLE_IP1 $SERVER_IP2 tcp dst port $SERVER_PORT2

sudo ip route add $SERVER_IP2/32 via $MIDDLE_IP1 dev $IF1

CMD=../../bin/init_connections_client
$CMD $SERVER_IP2 $SERVER_PORT2 $1
