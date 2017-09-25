#!/bin/bash

ulimit -n 65535

source config.sh

sudo rmmod dysco
sudo insmod $DYSCO_MODULE

sleep 1

sudo ip route add $CLIENT_IP1/32 via $MIDDLE_IP1 dev $IF2

CMD=../../bin/init_connections_server
$CMD $SERVER_PORT1 $1
