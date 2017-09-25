#!/bin/bash

TIMEOUT=1
TIME=`date "+%Y%m%d_%H%M%S"`

USER=`whoami`
REXEC="expect -re \"(#|\\\\$) $\"; send \""
REXEC_END="expect -re \"(#|\\\\$) $\""

EVALDIR=/home/ubuntu/eval_dysco/dysco_sigcomm17/evaluation/firewall_migration

source config.sh

get_git_password()
{
    echo -n "[sudo] git password: "
    stty -echo
    read PW
    stty echo
    echo
}

remote_exec()
{
    expect -c "
              set timeout $TIMEOUT
              spawn ssh $1
              $2
              expect -re \"(#|\\\\$) $\" ;  send \"exit\r\"
              interact
              "
}

update()
{
    remote_exec $1 "
                   $REXEC cd $EVALDIR\n\"
                   $REXEC git pull\n\"
                   expect -re \"Password for*:\" ; send \"$PW\n\"
                   "
}

enable_offload()
{
    remote_exec $1 "
                   $REXEC sudo ethtool -K eno1 tx on tso on gso on\n\"
                   $REXEC sudo ethtool -K eno1 rx on gro on lro on\n\"
                   $REXEC sudo ethtool -K eno2 tx on tso on gso on\n\"
                   $REXEC sudo ethtool -K eno2 rx on gro on lro on\n\"
                   "
}

disable_offload()
{
    remote_exec $1 "
                   $REXEC sudo ethtool -K eno1 tx off tso off gso off\n\"
                   $REXEC sudo ethtool -K eno1 rx off gro off lro off\n\"
                   $REXEC sudo ethtool -K eno2 tx off tso off gso off\n\"
                   $REXEC sudo ethtool -K eno2 rx off gro off lro off\n\"
                   "
}

init_client()
{
    # $2: NIC, $3: OLD_IP, $4: NEW_IP
    remote_exec $1 "
                   $REXEC cd $EVALDIR\n\"
                   $REXEC ./init_client.sh $2 $3 $4\n\"
                   "
}

init_server()
{
    # $2: NIC, $3: OLD_IP, $4: NEW_IP
    remote_exec $1 "
                   $REXEC cd $EVALDIR\n\"
                   $REXEC ./init_server.sh $2 $3 $4\n\"
                   "
}

init_router()
{
    remote_exec $1 "
                   $REXEC cd $EVALDIR\n\"
                   $REXEC ./init_router.sh\n\"
                   "
}

init_firewall()
{
    # $2: NIC, $3: OLD_IP, $4: NEW_IP
    # $5: NIC, $6: OLD_IP, $7: NEW_IP
    # $8: gw for client, $0 gw for server
    remote_exec $1 "
                   $REXEC cd $EVALDIR\n\"
                   $REXEC ./init_fw.sh $2 $3 $4 $5 $6 $7 $8 $9\n\"
                   "
}

cleanup()
{
    remote_exec $1 "
                   $REXEC killall iperf\n\"
                   $REXEC killall tcp_proxy\n\"
                   $REXEC killall dysco_daemon\n\"
                   $REXEC sleep 1\n\"
                   $REXEC sudo rmmod dysco\n\"
                   "
}

run_client()
{
    remote_exec $1 "
                   $REXEC cd $EVALDIR\n\"
                   $REXEC ./run_client.sh $2 $3 $4 $5 $6 $7 $8 $9 &\n\"
                   "
}

run_server()
{
    remote_exec $1 "
                   $REXEC cd $EVALDIR\n\"
                   $REXEC ./run_server.sh $2 $3\n\"
                   "
}

run_firewall1()
{
    # CLIENT, SERVER, MIDDLE1, MIDDLE2
    remote_exec $1 "
                   $REXEC cd $EVALDIR\n\"
                   $REXEC ./run_fw1.sh $2 $3 $4 $5 &\n\"
                   "
}

run_firewall2()
{
    remote_exec $1 "
                   $REXEC cd $EVALDIR\n\"
                   $REXEC ./run_fw2.sh\n\"
                   "
}

############# main #############

## update all test scripts in each host
get_git_password
update $HOST01
update $HOST02
update $HOST03
update $HOST04
update $HOST05
update $HOST06a
update $HOST07a
update $HOST08a
update $HOST09a
update $HOST10

## change the result directory
#enable_offload $HOST01
#enable_offload $HOST02
#enable_offload $HOST03
#enable_offload $HOST04
#enable_offload $HOST05
#enable_offload $HOST06a
#enable_offload $HOST07a
#enable_offload $HOST08a
#enable_offload $HOST09a
#enable_offload $HOST10
#disable_offload $HOST01
#disable_offload $HOST02
#disable_offload $HOST03
#disable_offload $HOST04
#disable_offload $HOST05
#disable_offload $HOST06a
#disable_offload $HOST07a
#disable_offload $HOST08a
#disable_offload $HOST09a
#disable_offload $HOST10
#sleep 10

cleanup $HOST01
cleanup $HOST02
cleanup $HOST03
cleanup $HOST04
cleanup $HOST05
cleanup $HOST06a
cleanup $HOST07a
cleanup $HOST08a
cleanup $HOST09a
cleanup $HOST10

init_router $ROUTER
init_client $CLIENT1 eno2 $CLIENT1_OLD $CLIENT1_IP
init_client $CLIENT2 eno2 $CLIENT2_OLD $CLIENT2_IP
init_client $CLIENT3 eno2 $CLIENT3_OLD $CLIENT3_IP
init_server $SERVER1 eno1 $SERVER1_OLD $SERVER1_IP
init_server $SERVER2 eno1 $SERVER2_OLD $SERVER2_IP
init_server $SERVER3 eno1 $SERVER3_OLD $SERVER3_IP
init_firewall $MIDDLE1 eno1 $MIDDLE1_OLD1 $MIDDLE1_IP1 \
              eno2 $MIDDLE1_OLD2 $MIDDLE1_IP2 $ROUTER_IP3 $ROUTER_IP4
init_firewall $MIDDLE2 eno1 $MIDDLE2_OLD1 $MIDDLE2_IP1 \
              eno2 $MIDDLE2_OLD2 $MIDDLE2_IP2 $ROUTER_IP5 $ROUTER_IP6

sleep 3

TIME_T=140
TIME_R=70
CONN=100

run_server $SERVER1 $PORT1 1
run_server $SERVER2 $PORT2 2
run_server $SERVER3 $PORT3 3

run_firewall2 $MIDDLE2
run_firewall1 $MIDDLE1 $CLIENT1_IP $SERVER1_IP $MIDDLE1_IP1 $MIDDLE2_IP1

sleep 3

run_client $CLIENT3 $TIME_T $CLIENT3_IP $MIDDLE1_IP1 $SERVER3_IP $PORT3 3 $CONN $TIME_R
run_client $CLIENT2 $TIME_T $CLIENT2_IP $MIDDLE1_IP1 $SERVER2_IP $PORT2 2 $CONN $TIME_R
run_client $CLIENT1 $TIME_T $CLIENT1_IP $MIDDLE1_IP1 $SERVER1_IP $PORT1 1 $CONN $TIME_R
