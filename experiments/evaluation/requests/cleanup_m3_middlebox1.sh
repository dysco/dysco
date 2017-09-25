#!/bin/bash

source config.sh

sudo killall -9 dysco_daemon
sleep 1
sudo rmmod dysco

sudo route del -host $SERVER3_IP

sudo sh -c 'echo 0 > /proc/sys/net/ipv4/ip_forward'
sudo sh -c 'echo 1 > /proc/sys/net/ipv4/conf/all/send_redirects'
sudo sh -c 'echo 1 > /proc/sys/net/ipv4/conf/all/accept_redirects'
