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

../../user/dysco_ctl policy $CLIENT_IP3 2 $MIDDLE_IP1 $SERVER_IP3 tcp dst port $SERVER_PORT3

sudo ip route add $SERVER_IP3/32 via $MIDDLE_IP1 dev $IF1

CMD=../../bin/init_connections_client
$CMD $SERVER_IP3 $SERVER_PORT3 $1
