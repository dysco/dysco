#!/bin/bash

ulimit -n 65535

source config.sh

#sudo rmmod dysco
#sudo insmod $DYSCO_MODULE
#
#sleep 1

sudo ip route add $CLIENT_IP/32 via $MIDDLE_IP2 dev $IF2

CMD=../../bin/server
$CMD $SERVER_PORT
