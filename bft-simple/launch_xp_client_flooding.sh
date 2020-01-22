#!/bin/bash

# launch an experiment: scp on the nodes machines, launch the programs, get the results.
#   $1: file which contains a list of client machines and the number of clients to run on it 
#   $2: req size (also equal to the rep size)


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

# 0. Stop all machines and setup the variables.
./sedinplace.sh CLIENT_FLOODING
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

# 3. Launch the clients
manager_ip=$(echo $MANAGER | awk -F':' '{print $2}')

# To count the total # of clients, the manager script will use this. 
NB_CLIENTS=0
nfaulty=-1
while read LINE
do
   machine=$(echo $LINE | awk '{print $1}')
   nbc=$(echo $LINE | awk '{print $2}')

   NB_CLIENTS=$(($NB_CLIENTS+$nbc))

   echo -n "Launching $nbc clients with $nfaulty faulty on $machine ..."
   ssh -n $machine "cd $BASE_DIR; nohup ./launch_clients.sh $manager_ip $nbc $nfaulty &> launch_clients_${machine}.log &"

   echo "DONE"

   if [ $nfaulty -eq -1 ]; then
      nfaulty=0
   fi
done < $CLIENTS_FILE


## # ====================================================
## 
# 4. Launch the manager


manager=$(echo $MANAGER | awk -F':' '{print $1}')
ssh -n $manager "cd $BASE_DIR; nohup ./launch_manager.sh $NB_CLIENTS $BURST_SIZE $WARMUP_PHASE $TOTAL_REQS $REQ_SIZE"


# ====================================================

## # 5. Killall & get results


OUTDIR="rrbft_${NB_CLIENTS}clients_${REQ_SIZE}B_client_flooding_$(date +%F_%H_%M)"
call_replica_sig_handler
sleep 20
get_results $OUTDIR

