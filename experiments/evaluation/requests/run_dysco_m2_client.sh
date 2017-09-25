#!/bin/bash

if [ $# -ne 1 ]; then
    echo "$0 <suffix of result file>"
    exit 1
fi

sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/all/send_redirects'
sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/all/accept_redirects'

source config.sh
source set_limits.sh

sudo killall -9 dysco_daemon > /dev/null
sudo rmmod dysco > /dev/null

sudo insmod $DYSCO_MODULE
sleep 1
$DYSCO_DAEMON &
sleep 1
sudo route add -host $SERVER2_IP gw $MIDDLE1_IP1

../../user/dysco_ctl policy $CLIENT_IP 3 $MIDDLE1_IP1 $MIDDLE2_IP1 $SERVER2_IP \
                     tcp dst port $SERVER_PORT

sleep 6

echo "start client..."
wrk -c 400 -d 15s -t 16 http://$SERVER2_IP:$SERVER_PORT/ > result/result_m2_dysco_$1.txt
#echo -n "any key to exit: "
#read ans
#
#sudo route del -host $SERVER2_IP
#
#sudo killall -9 dysco_daemon
#sleep 1
#sudo rmmod dysco
