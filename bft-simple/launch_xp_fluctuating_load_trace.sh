#!/bin/bash

# launch an experiment where the client use an existing trace
#   $1: file which contains a list of client machines and the number of clients to run on it 
#   $2: req size (also equal to the rep size)
#   $3: directory where we can find the trace


if [ $# -eq 3 ] ; then
    CLIENTS_FILE=$1
    REQ_SIZE=$2
    TRACE_DIR=$3
else
    echo "Usage ./$(basename $0) <clients_file> <req_size> <trace_dir>"
    exit 1
fi

# Source this file to get the stop function used below.
source util.sh

# ====================================================

# 0. Stop all machines and setup the variables.
./sedinplace.sh FLUCTUATING_LOAD_TRACE
stop_all_nodes_without_svn_up

# ====================================================

# 1. Launch the Verifier and the PIRs.
for replica in $VERIFIERS; do
    machine=$(echo $replica | awk -F':' '{print $1}')
    echo -n "Replica $machine ..."
    ssh -n $machine "cd $BASE_DIR; nohup ./launch_verifier_pirs.sh &> launch_verifier_pirs.log &"
    echo "DONE"
done

# ====================================================

# 2. Launch the clients
manager_ip=$(echo $MANAGER | awk -F':' '{print $2}')

# To count the total # of clients, the manager script will use this. 
NB_CLIENTS=0
start_id=0
while read LINE
do
    machine=$(echo $LINE | awk '{print $1}')
    nbc=$(echo $LINE | awk '{print $2}')

    echo -n "Launching $nbc clients on $machine ..."
    ssh -n $machine "cd $BASE_DIR; nohup ./launch_clients_fluctuating_load.sh $manager_ip $nbc $start_id $TRACE_DIR &> launch_clients_${machine}.log &"
    echo "DONE"

    NB_CLIENTS=$(($NB_CLIENTS+$nbc))
    start_id=$(($start_id+$nbc))

done < $CLIENTS_FILE


## # ====================================================
## 
# 3. Launch the manager
manager=$(echo $MANAGER | awk -F':' '{print $1}')
ssh -n $manager "cd $BASE_DIR; nohup ./launch_manager.sh $NB_CLIENTS $BURST_SIZE $WARMUP_PHASE $TOTAL_REQS $REQ_SIZE"

# ====================================================


call_replica_sig_handler
sleep 30

## # 4. Killall & get results
OUTDIR="rrbft_${REQ_SIZE}B_fluctuating_load_trace_${NB_CLIENTS}clients_$(date +%F_%H_%M)"
get_results $OUTDIR

