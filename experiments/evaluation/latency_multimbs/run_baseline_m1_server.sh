#!/bin/bash

sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/all/send_redirects'
sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/all/accept_redirects'

ulimit -n 65535

source config.sh

sudo rmmod dysco > /dev/null

sudo route add -host $CLIENT_IP gw $MIDDLE1_IP2

echo "start server..."
../../bin/server $SERVER_PORT &

#echo -n "Type any key to exit: "
#read ans
#
#killall -9 server
#sudo route del -host $CLIENT_IP gw $MIDDLE1_IP2
