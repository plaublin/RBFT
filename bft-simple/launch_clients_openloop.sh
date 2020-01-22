#!/bin/bash

# script used to launch some clients and 2 sar (1 for CPU, 1 for NET)
# OPEN LOOP
# note: you need to redirect the output of the manager to a file by yourself
# $1: manager IP.
# $2: number of clients to launch on this machine.
# $3: number of faulty clients to launch on this machine.
#     If -1, then launch a single faulty client that floods the system

if [ $# -eq 3 ]; then
MANAGER_IP=$1
NB_CLIENTS=$2   
NB_FAULTY_CLIENTS=$3
elif [ $# -eq 2 ]; then
MANAGER_IP=$1
NB_CLIENTS=$2
NB_FAULTY_CLIENTS=0
else
   echo "Args: <manager> <nb_clients>"
   echo "Or: Args: <manager> <nb_clients> <nb_faulty_clients>"
   exit
fi

CONFIG="scis/config.clients"
CONFIG_PRIV="config_private/template"

# launch sar during at most 1 day
DURATION=$((24*60*60))


if [ $NB_FAULTY_CLIENTS -eq -1 ]; then
    NB_CORRECT_CLIENTS=$NB_CLIENTS
else
    NB_CORRECT_CLIENTS=$(($NB_CLIENTS-$NB_FAULTY_CLIENTS))
fi
echo -e "NB CLIENTS=$NB_CLIENTS\nNB_CORRECT_CLIENTS=$NB_CORRECT_CLIENTS\nNB_FAULTY_CLIENTS=$NB_FAULTY_CLIENTS"

taskset -c 0 sar -n DEV 1 $DURATION > net_clients_$(hostname).log &
taskset -c 1 sar -P ALL 1 $DURATION > cpu_clients_$(hostname).log &

NB_CORES=$(grep cores /proc/cpuinfo | wc -l)

DEBUG="GDB"

core=0
i=0
j=0
for PORT in `grep $(hostname) $CONFIG | awk '{print $3}'` ; do
  if [ $i -lt $NB_CORRECT_CLIENTS ] ; then
     echo "Launching a correct client..."
     if [ -z $DEBUG ]; then
       ./client_openloop -c $CONFIG -C $CONFIG_PRIV -p $PORT -g 0 -m $MANAGER_IP 2>&1  &
     elif [ "$DEBUG" = "GDB" ]; then
        ########### GDB ##########
        echo "handle SIGINT pass" > /tmp/client_${i}.gdb
        echo "handle SIGINT nostop" >> /tmp/client_${i}.gdb
        echo "handle SIGPIPE pass" >> /tmp/client_${i}.gdb
        echo "handle SIGPIPE nostop" >> /tmp/client_${i}.gdb
        echo "run -c $CONFIG -C $CONFIG_PRIV -p $PORT -g 0 -m $MANAGER_IP" >> /tmp/client_${i}.gdb
        echo "bt" >> /tmp/client_${i}.gdb
        echo "quit" >> /tmp/client_${i}.gdb
        gdb -x /tmp/client_${i}.gdb ./client_openloop &> client_$(hostname)_${i}.log &
     else
        echo "Unknown $DEBUG"
     fi

     i=$(($i+1))
     core=$((($core+1)%$NB_CORES))
  else
     if [ $j -lt $NB_FAULTY_CLIENTS ] ; then
       echo "Launching a byzantine client..."
       ./client_openloop -c $CONFIG -C $CONFIG_PRIV -p $PORT -g 1 -m $MANAGER_IP 2>&1  &
       j=$(($j+1))
       core=$((($core+1)%$NB_CORES))
     else
       if [ $NB_FAULTY_CLIENTS -eq -1 ]; then     
          # launch the flooder
          echo "Launching a flooder client..."
          ./client_openloop -c $CONFIG -C $CONFIG_PRIV -p $PORT -g 0 -m $MANAGER_IP -f 2>&1  &
       fi
       break
     fi
  fi
done




sleep 2
echo "Done."
