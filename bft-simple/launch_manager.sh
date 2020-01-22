#!/bin/bash

# script used to launch the manager
# note: you need to redirect the output of the manager to a file by yourself
# $1: nb clients
# $2: nb req per bursts
# $3: nb reqs before logging
# $4: nb reqs before stopping the experiment
# $5: req size


if [ $# -eq 5 ]; then
   NB_CLIENTS=$1
   NB_REQ=$2
   NB_REQBEFORELOGGING=$3
   NB_REQBEFORSTOPPING=$4
   REQ_SIZE=$5

else
   echo "Args: <nb clients> <nb_req_per_bursts> <nb_req_before_logging> <nb_req_before_stopping_xp> <req_size>"
   exit 0
fi

echo "nb_req_before_logging=$NB_REQBEFORELOGGING; nb_req_before_stopping_xp=$NB_REQBEFORSTOPPING; req_size=$REQ_SIZE"

source util.sh

#are_there_clients_or_stop_all

#./bft_manager -f $NB_FAULTS -c $NB_CLIENTS -n $NB_REQ -t $TIMEOUT -b $NB_REQBEFORELOGGING -l $NB_REQBEFORSTOPPING -s $REQ_SIZE 2>&1

# using timelimit. 20 minutes per XP at most
timelimit -s 9 -t $((15*60)) ./bft_manager -f $NB_FAULTS -c $NB_CLIENTS -n $NB_REQ -t $TIMEOUT -b $NB_REQBEFORELOGGING -l $NB_REQBEFORSTOPPING -s $REQ_SIZE 2>&1
