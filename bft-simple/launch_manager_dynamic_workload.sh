#!/bin/bash

# script used to launch the manager
# note: you need to redirect the output of the manager to a file by yourself
# $1: max nb clients
# $2: xp file


if [ $# -eq 2 ]; then
   NB_CLIENTS=$1
   XP_FILE=$2

else
   echo "Args: <nb clients> <xp_file>"
   exit 0
fi

source util.sh

timelimit -s 9 -t $((20*60)) ./manager_dynamic_workload -f $NB_FAULTS -c $NB_CLIENTS -x $XP_FILE 2>&1

#echo "run -f $NB_FAULTS -c $NB_CLIENTS -x $XP_FILE" > /tmp/batch_manager.gdb
#echo "bt" >> /tmp/batch_manager.gdb
#echo "quit" >> /tmp/batch_manager.gdb
#gdb -x /tmp/batch_manager.gdb ./manager_dynamic_workload 2>&1
