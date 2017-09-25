#!/bin/bash

source config.sh

NIC1=eno2:0
sudo ifdown $NIC1
sudo ifconfig $NIC1 $ROUTER_IP1 netmask 255.255.255.0 up

NIC2=eno1:0
sudo ifdown $NIC2
sudo ifconfig $NIC2 $ROUTER_IP2 netmask 255.255.255.0 up

NIC10=eno1:1
sudo ifconfig $NIC10 $ROUTER_IP3 netmask 255.255.255.0 up

NIC20=eno2:1
sudo ifconfig $NIC20 $ROUTER_IP4 netmask 255.255.255.0 up

sudo sh -c 'echo 1 > /proc/sys/net/ipv4/ip_forward'
## to avoid icmp redirect when using virtual interface
# sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/all/send_redirects'
# sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/all/accept_redirects'

sudo rmmod dysco
