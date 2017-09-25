#!/bin/bash

TIMEOUT=1
TIME=`date "+%Y%m%d_%H%M%S"`

USER=`whoami`
REXEC="expect -re \"(#|\\\\$) $\"; send \""
REXEC_END="expect -re \"(#|\\\\$) $\""

EVALDIR=/home/ubuntu/dysco/sigcomm/eval/evaluation/throughput

source config.sh

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

check()
{
    remote_exec $1 "
                   $REXEC echo SUCCESS \`hostname\`\n\"
                   "
}

############# main #############

# update all test scripts in each host
check $HOST01
check $HOST02
check $HOST03
check $HOST04
check $HOST05
check $HOST06
check $HOST07
check $HOST08
check $HOST09
check $HOST10
