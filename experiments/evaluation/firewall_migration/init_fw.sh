#!/bin/sh

NIC1=$1
sudo ifdown $NIC1
sudo ifconfig $NIC1:0 $2 netmask 255.255.255.0 up
sudo ifconfig $NIC1 $3 netmask 255.255.255.0 up

NIC2=$4
sudo ifdown $NIC2
sudo ifconfig $NIC2:0 $5 netmask 255.255.255.0 up
sudo ifconfig $NIC2 $6 netmask 255.255.255.0 up

sudo route add -net 10.0.1.0/24 gw $7
sudo route add -net 10.0.3.0/24 gw $7
sudo route add -net 10.0.5.0/24 gw $7

sudo route add -net 10.0.2.0/24 gw $8
sudo route add -net 10.0.4.0/24 gw $8
sudo route add -net 10.0.6.0/24 gw $8

# tc
BW=2000
sudo tc qdisc del dev $NIC1 root
sudo tc qdisc add dev $NIC1 handle 1: root htb default 11
sudo tc class add dev $NIC1 parent 1: classid 1:1 htb rate ${BW}mbit
sudo tc class add dev $NIC1 parent 1:1 classid 1:11 htb rate ${BW}mbit
sudo tc qdisc del dev $NIC2 root
sudo tc qdisc add dev $NIC2 handle 1: root htb default 11
sudo tc class add dev $NIC2 parent 1: classid 1:1 htb rate ${BW}mbit
sudo tc class add dev $NIC2 parent 1:1 classid 1:11 htb rate ${BW}mbit

sudo sysctl net.ipv4.tcp_sack=1
sudo sh -c 'echo 0 > /proc/sys/net/ipv4/tcp_timestamps'
sudo sh -c 'echo 1 > /proc/sys/net/ipv4/ip_forward'
sudo sh -c 'echo 1 > /proc/sys/net/netfilter/nf_conntrack_tcp_be_liberal'

sudo killall -9 tcp_proxy
sudo killall -9 iperf
sudo killall -9 netperf
sudo killall -9 dysco_daemon
sudo rmmod dysco
./iptables.sh stop
