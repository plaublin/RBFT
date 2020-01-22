#!/bin/bash

# script used to launch some clients and 2 sar (1 for CPU, 1 for NET)
# note: you need to redirect the output of the manager to a file by yourself
# $1: manager IP.
# $2: number of clients to launch on this machine.
# $3: proportion of correct requests. Optionnal.

if [ $# -eq 3 ]; then
   MANAGER_IP=$1
   NB_CLIENTS=$2
   PROPORTION_CORRECT_REQUESTS=$3

elif [ $# -eq 2 ]; then
   MANAGER_IP=$1
   NB_CLIENTS=$2
   PROPORTION_CORRECT_REQUESTS=1.0
else
   echo "Args: <manager> <nb_clients> [proportion_correct_requests]"
   exit
fi

CONFIG="scis/config.clients"
CONFIG_PRIV="config_private/template"

# launch sar during at most 1 day
DURATION=$((24*60*60))

echo -e "NB CLIENTS=$NB_CLIENTS\nSTART_ID=$START_ID\nTRACE_DIR=$TRACE_DIR"

taskset -c 0 sar -n DEV 1 $DURATION > net_clients_$(hostname).log &
taskset -c 1 sar -P ALL 1 $DURATION > cpu_clients_$(hostname).log &

NB_CORES=$(grep cores /proc/cpuinfo | wc -l)

core=0
i=0
for PORT in `grep $(hostname) $CONFIG | awk '{print $3}'` ; do
  if [ $i -lt $NB_CLIENTS ] ; then
     taskset -c $core ./client_dynamic_workload -c $CONFIG -C $CONFIG_PRIV -p $PORT -m $MANAGER_IP -g $PROPORTION_CORRECT_REQUESTS 2>&1  &

    ########### GDB ##########
#    echo "run -c $CONFIG -C $CONFIG_PRIV -p $PORT -m $MANAGER_IP" > /tmp/batch_client$i.gdb
#    echo "bt" >> /tmp/batch_client$i.gdb
#    echo "quit" >> /tmp/batch_client$i.gdb
#    gdb -x /tmp/batch_client$i.gdb ./client_dynamic_workload 2>&1 &

  else
     break
  fi

  i=$(($i+1))
  core=$((($core+1)%$NB_CORES))
done

sleep 2
echo "Done."
