#!/bin/bash

TIMEOUT=10
TIME=`date "+%Y%m%d_%H%M%S"`

USER=`whoami`
REXEC="expect -re \"(#|\\\\$) $\"; send \""
REXEC_END="expect -re \"(#|\\\\$) $\""

EVALDIR=/home/ubuntu/eval_dysco/dysco_sigcomm17/evaluation/goodput

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
              expect eof
              "
}

update()
{
    remote_exec $1 "
                   $REXEC cd $EVALDIR\n\"
                   $REXEC git checkout .\n\"
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

init_proxy()
{
    # $2: NIC, $3: OLD_IP, $4: NEW_IP
    # $5: NIC, $6: OLD_IP, $7: NEW_IP
    remote_exec $1 "
                   $REXEC cd $EVALDIR\n\"
                   $REXEC ./init_proxy_baseline.sh $2 $3 $4 $5 $6 $7\n\"
                   "
}

cleanup()
{
    remote_exec $1 "
                   $REXEC killall netperf\n\"
                   $REXEC killall tcp_proxy\n\"
                   $REXEC killall dysco_module\n\"
                   $REXEC sudo rmmod dysco\n\"
                   "
}

run_client()
{
    remote_exec $1 "
                   $REXEC cd $EVALDIR\n\"
                   $REXEC ./run_client_baseline.sh $2 $3 $4 $5 $6 $7 $8 &\n\"
                   "
}

run_server()
{
    remote_exec $1 "
                   $REXEC cd $EVALDIR\n\"
                   $REXEC ./run_server_baseline.sh $2 $3\n\"
                   "
}

#run_proxy()
#{
#    remote_exec $1 "
#                   $REXEC cd $EVALDIR\n\"
#                   $REXEC ./run_proxy.sh $2 $3 $4 $5 $6\n\"
#                   "
#}

############# main #############

# update all test scripts in each host
#get_git_password
#update $HOST01
#update $HOST02
#update $HOST03
#update $HOST04
#update $HOST05
#update $HOST06a
#update $HOST07a
#update $HOST08a
#update $HOST09a
#update $HOST10

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
disable_offload $HOST01
disable_offload $HOST02
disable_offload $HOST03
disable_offload $HOST04
disable_offload $HOST05
disable_offload $HOST06
disable_offload $HOST07
disable_offload $HOST08
disable_offload $HOST09
disable_offload $HOST10

cleanup $HOST01
cleanup $HOST02
cleanup $HOST03
cleanup $HOST04
cleanup $HOST05
cleanup $HOST06
cleanup $HOST07
cleanup $HOST08
cleanup $HOST09
cleanup $HOST10

##init_router $ROUTER
init_client $CLIENT1 eno1 $CLIENT1_OLD $CLIENT1_IP
init_client $CLIENT2 eno1 $CLIENT2_OLD $CLIENT2_IP
init_client $CLIENT3 eno1 $CLIENT3_OLD $CLIENT3_IP
init_client $CLIENT4 eno1 $CLIENT4_OLD $CLIENT4_IP
init_server $SERVER1 eno2 $SERVER1_OLD $SERVER1_IP
init_server $SERVER2 eno2 $SERVER2_OLD $SERVER2_IP
init_server $SERVER3 eno2 $SERVER3_OLD $SERVER3_IP
init_server $SERVER4 eno2 $SERVER4_OLD $SERVER4_IP
init_proxy $PROXY eno1 $PROXY_OLD1 $PROXY_IP1 eno2 $PROXY_OLD2 $PROXY_IP2

sleep 3

TIME_T=30
CONN=$1

run_server $SERVER1 $PROXY_PORT1 1
run_server $SERVER2 $PROXY_PORT2 2
run_server $SERVER3 $PROXY_PORT3 3
run_server $SERVER4 $PROXY_PORT4 4

sleep 10

run_client $CLIENT1 $TIME_T $CLIENT1_IP $PROXY_IP1 $PROXY_PORT1 1 $CONN $SERVER1_IP
run_client $CLIENT2 $TIME_T $CLIENT2_IP $PROXY_IP1 $PROXY_PORT2 2 $CONN $SERVER2_IP
run_client $CLIENT3 $TIME_T $CLIENT3_IP $PROXY_IP1 $PROXY_PORT3 3 $CONN $SERVER3_IP
run_client $CLIENT4 $TIME_T $CLIENT4_IP $PROXY_IP1 $PROXY_PORT4 4 $CONN $SERVER4_IP
