#!/bin/bash

source config.sh

sudo rmmod dysco

sudo ip route add $CLIENT_IP/32 via $MIDDLE_IP2 dev $IF2

CMD=../../bin/server
$CMD $SERVER_PORT
