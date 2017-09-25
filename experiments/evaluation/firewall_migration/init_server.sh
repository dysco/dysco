#!/bin/sh

NIC=$1
OLD_IP=$2
NEW_IP=$3

sudo ifdown $NIC

sudo ifconfig $NIC:0 $OLD_IP netmask 255.255.255.0 up

sudo ifconfig $NIC $NEW_IP netmask 255.255.255.0 up

sudo route add -net 10.0.0.0/16 gw 10.0.2.1

sudo sysctl net.ipv4.tcp_sack=1
sudo sh -c 'echo 0 > /proc/sys/net/ipv4/tcp_timestamps'
sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/default/rp_filter'
sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/all/rp_filter'
sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/eno1/rp_filter'
sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/eno2/rp_filter'

sudo killall -9 tcp_proxy
sudo killall -9 iperf
sudo killall -9 netperf
sudo killall -9 dysco_daemon
sudo rmmod dysco
