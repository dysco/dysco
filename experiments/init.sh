#!/bin/sh

DIR=`pwd`
BIN=$DIR/bin

mkdir $BIN > /dev/null 2>&1 

cd ./src/latency
make clean
make
cp *client *server $BIN
cd -

cd ./src/middlebox/go/src/mb2
go get github.com/google/gopacket
go get github.com/google/gopacket/pcap
go build mb2.go
cp mb2 $BIN
cd -

cd ./src/middlebox/go/src/move_iptables
go get github.com/pborman/getopt 
go build move_iptables.go
cp move_iptables $BIN
cd -
