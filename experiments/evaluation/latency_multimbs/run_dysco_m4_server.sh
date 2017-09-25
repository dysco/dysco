#!/bin/bash

sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/all/send_redirects'
sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/all/accept_redirects'

ulimit -n 65535

source config.sh

sudo rmmod dysco > /dev/null
sudo insmod $DYSCO_MODULE

sleep 1

sudo route add -host $CLIENT_IP gw $MIDDLE4_IP2

echo "start server..."
../../bin/server $SERVER_PORT &

#echo -n "any key to exit: "
#read ans
#
#killall -9 server
#sudo route del -host $CLIENT_IP
#sudo rmmod dysco
