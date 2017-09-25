#!/bin/bash

ulimit -n 65535

source config.sh

sudo sh -c 'echo 1 > /proc/sys/net/ipv4/ip_forward'
sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/all/send_redirects'
sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/all/accept_redirects'

sudo rmmod dysco > /dev/null
sudo insmod $DYSCO_MODULE

sleep 1

sudo route add -host $CLIENT_IP gw $MIDDLE1_IP2
sudo route add -host $SERVER4_IP gw $MIDDLE3_IP1

#echo -n "any key to exit: "
#read ans
#
#sudo rmmod dysco
#
#sudo route del -host $CLIENT_IP
#sudo route del -host $SERVER4_IP
#
#sudo sh -c 'echo 0 > /proc/sys/net/ipv4/ip_forward'
#sudo sh -c 'echo 1 > /proc/sys/net/ipv4/conf/all/send_redirects'
#sudo sh -c 'echo 1 > /proc/sys/net/ipv4/conf/all/accept_redirects'
