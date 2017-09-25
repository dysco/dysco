#!/bin/bash

sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/all/send_redirects'
sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/all/accept_redirects'

source config.sh
source set_limits.sh

sudo rmmod dysco > /dev/null

sudo route add -host $CLIENT_IP gw $MIDDLE3_IP2

echo "start server... no need, nginx already running"
#../../bin/server $SERVER_PORT &

#echo -n "any key to exit: "
#read ans
#
#killall -9 server
#sudo route del -host $CLIENT_IP
