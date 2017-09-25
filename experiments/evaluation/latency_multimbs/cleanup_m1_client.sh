#!/bin/bash

source config.sh

sudo route del -host $SERVER1_IP

sudo killall -9 dysco_daemon client
sleep 1
sudo rmmod dysco

sudo sh -c 'echo 1 > /proc/sys/net/ipv4/conf/all/send_redirects'
sudo sh -c 'echo 1 > /proc/sys/net/ipv4/conf/all/accept_redirects'
