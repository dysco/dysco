#!/bin/bash

source config.sh

sudo sh -c 'echo 1 > /proc/sys/net/ipv4/ip_forward'
sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/all/send_redirects'
sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/all/accept_redirects'

sudo rmmod dysco > /dev/null
sudo insmod $DYSCO_MODULE

sleep 1

#echo -n "Type any key to exit: "
#read ans
#
#sudo rmmod dysco
