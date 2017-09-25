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

sudo route add -host $SERVER4_IP gw $MIDDLE1_IP1

sleep 5

echo "start client..."
wrk -c 400 -d 15s -t 16 http://$SERVER4_IP:$SERVER_PORT/ > result/result_m4_baseline_$1.txt
#echo -n "any key to exit: "
#read ans
#
#sudo route del -host $SERVER4_IP
