#!/bin/bash

source config.sh

sudo route del -host $SERVER1_IP

sudo killall -9 dysco_daemon 
sleep 3
sudo rmmod dysco

sudo sh -c 'echo 1 > /proc/sys/net/ipv4/conf/all/send_redirects'
sudo sh -c 'echo 1 > /proc/sys/net/ipv4/conf/all/accept_redirects'
source unset_limits.sh
