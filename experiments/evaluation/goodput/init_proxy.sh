#!/bin/sh

NIC1=$1
sudo ifdown $NIC1
sudo ifconfig $NIC1:0 $2 netmask 255.255.255.0 up
sudo ifconfig $NIC1 $3 netmask 255.255.255.0 up

NIC2=$4
sudo ifdown $NIC2
sudo ifconfig $NIC2:0 $5 netmask 255.255.255.0 up
sudo ifconfig $NIC2 $6 netmask 255.255.255.0 up

#sudo route add -net 10.0.1.0/24 gw 10.0.3.1
#sudo route add -net 10.0.2.0/24 gw 10.0.4.1

#sudo sysctl net.ipv4.tcp_sack=1
sudo sh -c 'echo 1 > /proc/sys/net/ipv4/ip_forward'

sudo killall -9 tcp_proxy
sudo killall -9 dysco_daemon

DYSCO=/home/ubuntu/dysco/sigcomm
DYSCO_MODULE=$DYSCO/nf/dysco.ko
sudo rmmod dysco
sudo insmod $DYSCO_MODULE
$DYSCO/user/dysco_daemon &

#timeout 140 dstat -c > results/cpu_proxy_0.txt &
#timeout -sKILL 142 sar -u -P ALL 1 > results/cpu_proxy_0.txt &
