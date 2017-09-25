#!/bin/bash

if [ $# -ne 1 ]; then
    echo "$0 <suffix of result file>"
    exit 1
fi

sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/all/send_redirects'
sudo sh -c 'echo 0 > /proc/sys/net/ipv4/conf/all/accept_redirects'

ulimit -n 65535

source config.sh

sudo killall -9 dysco_daemon > /dev/null
sudo rmmod dysco > /dev/null

sudo insmod $DYSCO_MODULE
sleep 1
$DYSCO_DAEMON &
sleep 1

sudo route add -host $SERVER4_IP gw $MIDDLE1_IP1

../../user/dysco_ctl policy $CLIENT_IP 5 $MIDDLE1_IP1 $MIDDLE2_IP1 $MIDDLE3_IP1 $MIDDLE4_IP1 \
                     $SERVER4_IP tcp dst port $SERVER_PORT

sleep 5

echo "start client..."
../../bin/client $SERVER4_IP $SERVER_PORT 1000 > result/result_m4_dysco_$1.txt
#echo -n "any key to exit: "
#read ans
#
#sudo route del -host $SERVER4_IP
#
#sudo killall -9 dysco_daemon
#sleep 1
#sudo rmmod dysco
