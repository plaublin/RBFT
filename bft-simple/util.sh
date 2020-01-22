#!/bin/bash

# Contains some util stuff that was present in multiple files before


BASE_DIR="~/RRBFT_v2/bft-simple"

# Number of requests during the warmup phase
if [ -z $WARMUP_PHASE ]; then
   WARMUP_PHASE=60000
fi

# Number of requests during the logging phase
if [ -z $LOGGING_PHASE ]; then
   LOGGING_PHASE=120000
fi

# size of a burst (i.e. number of requests each client is executing during one burst)
if [ -z $BURST_SIZE ]; then
   BURST_SIZE=1000
fi

# Total number of requests before stoppping the experiment
TOTAL_REQS=$(($WARMUP_PHASE+$LOGGING_PHASE))

CHROOT_32_BITS="/32bits/usr/lib"

if [ -d $CHROOT_32_BITS ]; then
   export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$CHROOT_32_BITS
fi

NB_CORES=$(($(grep cores /proc/cpuinfo | wc -l) + 1))

TIMEOUT=300
NB_FAULTS=1

CONFIG_PRIV="config_private/template"

# launch sar during at most 1 day
DURATION=$((24*60*60))

# We set the size of the sliding window to 1
SLIDING_WINDOW_SIZE=1

source machines.sh


# stop all the nodes, on all the machines
# $1: attack
function stop_all_nodes {
for node in $NODES; do
   echo -n "Stoppping node $node..."
   #ssh -n $node "cd $BASE_DIR/..; svn up"
   #ssh -n $node "cd $BASE_DIR; ./stop_all.sh; rm -f *.dat *.log; ./compile.sh"
   ssh -n $node "cd $BASE_DIR; ./stop_all.sh"

   echo "DONE"
done
}


function update_compile_current_node {
  cd ${BASE_DIR}/../
  svn up
  cd bft-simple
  ./compile.sh
}


# kill -2 the replicas
function call_replica_sig_handler {
for replica in $VERIFIERS; do
   machine=$(echo $replica | awk -F':' '{print $1}')
   echo "Calling sig handler for verifier and pir on $machine..."
   ssh -n $machine "pkill -2 bft_verifier"
   ssh -n $machine "pkill -2 bft_pir"
   echo "DONE"
done
}

# Stop all nodes and do not perform svn up, but rather rsync.
function stop_all_nodes_without_svn_up {

CURRENT=`pwd`

for node in $NODES; do
   echo "#######################################################"
   echo "Stoppping node $node (and not  launching svn up)..."

   ssh -n $node "cd $BASE_DIR; rm bft_pir bft_verifier bft_client bft_manager >/dev/null 2>/dev/null"

   rsync -rvazc -e 'ssh' --exclude-from './exclude_from_rsync.txt' '../' "${node}:${BASE_DIR}/../"

   ssh -n $node "cd $BASE_DIR; rm ../libbyz.pir/*.a >/dev/null 2>/dev/null; rm ../libbyz.verifier/*.a >/dev/null 2>/dev/null; ./stop_all.sh; ./compile.sh" 

   echo "DONE"
   echo "#######################################################"
done
}

# killall and get results
# $1: the directory where to put the results
function get_results {
OUTDIR="$1"
mkdir $OUTDIR
echo "Output directory is $OUTDIR"

# Getting results from the manager
if [ -z $NO_MANAGER ]; then
   echo -n "Getting results from the manager ..."
   ssh -n $manager "cd $BASE_DIR; ./stop_all.sh"
   cd $OUTDIR
   scp $manager:$BASE_DIR/manager.log .
   cd -
   ssh -n $manager "rm $BASE_DIR/manager.log"
   echo "DONE"
fi

# Getting results from the clients

# launch a total of NB_CLIENTS clients, with NB_CLIENTS_PER_MACHINE clients per machine
if [ -z $CLIENTS_FILE ]; then

   echo -n "Getting results from the clients ..."
   NB_CLIENTS2=$NB_CLIENTS
   for client in $CLIENTS; do
      machine=$(echo $client | awk -F':' '{print $1}')

      if [ $NB_CLIENTS2 -gt $NB_CLIENTS_PER_MACHINE ]; then
         nbc=$NB_CLIENTS_PER_MACHINE
         NB_CLIENTS2=$(( $NB_CLIENTS2 - $NB_CLIENTS_PER_MACHINE ))
      else
         nbc=$NB_CLIENTS2
         NB_CLIENTS2=0
      fi

      ssh -n $machine "cd $BASE_DIR/; ./stop_all.sh; tar cfz results.tar.gz *.log *.dat *.core"
      cd $OUTDIR
      scp $machine:$BASE_DIR/results.tar.gz .
      tar xfz results.tar.gz
      rm results.tar.gz
      cd -
      ssh -n $machine "cd $BASE_DIR/; rm *.log *.dat *.core results.tar.gz &>/dev/null"

      if [ $NB_CLIENTS2 -eq 0 ]; then
         break
      fi
   done 

   # launch the clients as described in the file $CLIENTS_FILE
else

   while read LINE
   do
      machine=$(echo $LINE | awk '{print $1}')

      ssh -n $machine "cd $BASE_DIR/; ./stop_all.sh; tar cfz results.tar.gz *.log *.dat *.core"
      cd $OUTDIR
      scp $machine:$BASE_DIR/results.tar.gz .
      tar xfz results.tar.gz
      rm results.tar.gz
      cd -
      ssh -n $machine "cd $BASE_DIR/; rm *.log *.dat *.core results.tar.gz &>/dev/null"
   done < $CLIENTS_FILE

fi
echo "DONE"

# Getting results from the replicas
echo -n "Getting results from the replicas ..."
for replica in $VERIFIERS; do
   machine=$(echo $replica | awk -F':' '{print $1}')

   ssh -n $machine "cd $BASE_DIR/; ./stop_all.sh; tar cfz results.tar.gz *.log *.dat *.core"
   cd $OUTDIR
   scp $machine:$BASE_DIR/results.tar.gz .
   tar xfz results.tar.gz
   rm results.tar.gz
   cd -
   ssh -n $machine "cd $BASE_DIR/; rm *.log *.core results.tar.gz &>/dev/null"
done 
echo "DONE"
}


# wait for the end of TIME_WAIT connections
function wait_for_time_wait {
nbc=1
while [ $nbc != 0 ]; do
   ./stop_all.sh
   echo "Waiting for the end of TIME_WAIT connections"
   sleep 20
   nbc=$(netstat -tn | grep TIME_WAIT | grep -v ":22 " | wc -l)
done
}

# if there are clients on this machine, then do not kill them.
# Used by the manager
function are_there_clients_or_stop_all {
# execute stop_all.sh only if there is no client on this machine
ps -A | grep client > /dev/null
res=$?
if [ $res -eq 1 ]; then
   ./stop_all.sh
fi
}

# launch sar
# $1: to identify the file
function launch_sar {
sar -n DEV 1 $DURATION > net_$1_$(hostname).log &
sar -P ALL 1 $DURATION > cpu_$1_$(hostname).log &
}
