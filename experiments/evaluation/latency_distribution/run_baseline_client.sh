#!/bin/bash

source config.sh

sudo killall -9 dysco_daemon

sleep 1

sudo rmmod dysco

sudo ip route add $SERVER_IP/32 via $MIDDLE_IP1 dev $IF1

sleep 5

CMD=../../bin/client
$CMD $SERVER_IP $SERVER_PORT 1000
