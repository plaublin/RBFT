#!/bin/bash

# script used to launch a verifier and 2 sar (1 for CPU, 1 for NET)
# note: you need to redirect the output of the manager to a file by yourself
# $1: config file


if [ $# -ne 1 ]; then
   echo "Args: <config_file>"
   echo "Using config.clients this time"
   CONFIG="config.clients"
else
   CONFIG=$1
fi

CONFIG_PRIV="config_private/template"
# launch sar during at most 1 day
DURATION=$((24*60*60))

#./stop_all.sh

taskset -c 1 sar -n DEV 1 $DURATION > net_replica_$(hostname).log &
taskset -c 2 sar -P ALL 1 $DURATION > cpu_replica_$(hostname).log &

taskset -c 0 ./verifier -c $CONFIG -p $CONFIG_PRIV 2>&1
