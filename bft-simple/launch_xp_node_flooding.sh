#!/bin/bash

# launch an experiment: scp on the nodes machines, launch the programs, get the results.
#   $1: file which contains a list of client machines and the number of clients to run on it 
#   $2: req size (also equal to the rep size)

FAULTY_NODE="sci77"

if [ $# -eq 2 ] ; then
  CLIENTS_FILE=$1
  REQ_SIZE=$2
else
  echo "Usage ./$(basename $0) <clients_file> <req_size>"
  exit 1
fi

# Source this file to get the stop function used below.
source util.sh

# ====================================================

# 0a. we need to define N_LE_GT_N_F_1 in libbyz.pir/parameters.h
# revert any change
#./sedinplace.sh FAULT_FREE
./sedinplace.sh NODE_FLOODING

# 0b. Stop all machines and setup the variables.
stop_all_nodes_without_svn_up

# ====================================================

# 1. Launch the Verifier and the PIRs.
for replica in $VERIFIERS; do
   machine=$(echo $replica | awk -F':' '{print $1}')
   if [ "$machine" == "${FAULTY_NODE}" ]; then
      ARGS="NODE_FLOODING"
   else
      ARGS=""
   fi

   echo -n "Replica $machine ..."
   ssh -n $machine "cd $BASE_DIR; nohup ./launch_verifier_pirs.sh ${ARGS} &> launch_verifier_pirs.log &"
   echo "DONE"
done

# ====================================================

 # 2. Launch the clients
manager_ip=$(echo $MANAGER | awk -F':' '{print $2}')
    
# To count the total # of clients, the manager script will use this. 
NB_CLIENTS=0

 while read LINE
 do
    machine=$(echo $LINE | awk '{print $1}')
    nbc=$(echo $LINE | awk '{print $2}')
 
    echo -n "Launching $nbc clients on $machine ..."
    ssh -n $machine "cd $BASE_DIR; nohup ./launch_clients.sh $manager_ip $nbc &> launch_clients_${machine}.log &"
    echo "DONE"
 
    NB_CLIENTS=$(($NB_CLIENTS+$nbc))
 
 done < $CLIENTS_FILE

 
 ## # ====================================================
 ## 
 # 3. Launch the manager
 manager=$(echo $MANAGER | awk -F':' '{print $1}')
 ssh -n $manager "cd $BASE_DIR; nohup ./launch_manager.sh $NB_CLIENTS $BURST_SIZE $WARMUP_PHASE $TOTAL_REQS $REQ_SIZE"

# ====================================================

## # 4. Killall & get results
OUTDIR="rrbft_${NB_CLIENTS}clients_${REQ_SIZE}B_node_flooding_$(date +%F_%H_%M)"
call_replica_sig_handler
sleep 20
get_results $OUTDIR

