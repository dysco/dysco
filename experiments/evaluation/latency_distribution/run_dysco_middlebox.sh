#!/bin/bash

ulimit -n 65535

source config.sh

sudo rmmod dysco
sudo insmod $DYSCO_MODULE

sleep 1

