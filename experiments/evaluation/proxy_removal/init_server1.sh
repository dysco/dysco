#!/bin/sh

NIC=eno1
sudo ifdown $NIC
sudo ifconfig $NIC 10.0.2.2 netmask 255.255.255.0 up

NIC=eno1:0
sudo ifconfig $NIC 192.168.3.143 netmask 255.255.255.0 up


sudo route add -net 10.0.0.0/16 gw 10.0.2.1


sudo sysctl net.ipv4.tcp_sack=1
sudo sh -c 'echo 0 > /proc/sys/net/ipv4/tcp_timestamps'


sudo rmmod dysco
