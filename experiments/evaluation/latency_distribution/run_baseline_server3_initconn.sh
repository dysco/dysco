#!/bin/bash

ulimit -n 65535

source config.sh

sudo rmmod dysco

sudo ip route add $CLIENT_IP3/32 via $MIDDLE_IP2 dev $IF2

CMD=../../bin/init_connections_server
$CMD $SERVER_PORT3 $1
