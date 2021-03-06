#!/bin/bash

# launch an experiment: scp on the nodes machines, launch the programs, get the results.
#   $1: file which contains a list of client machines and the number of clients to run on it 
#   $2: req size (also equal to the rep size)
#   $3: percentage of faulty clients


if [ $# -eq 3 ] ; then
   CLIENTS_FILE=$1
   REQ_SIZE=$2
   PERCENTAGE_C=$3
else
   echo "Usage ./$(basename $0) <clients_file> <req_size> <percentage_of_good_clients>"
   exit 1
fi

# Source this file to get the stop function used below.
source util.sh

# ====================================================

# 0. Stop all machines and setup the variables.
./sedinplace.sh CLIENTS_PROPORTION_G
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

# 2. get the total number of clients and the number of faulty clients
NB_CLIENTS=0
NB_CLIENT_MACHINES=0
while read LINE
do
   nbc=$(echo $LINE | awk '{print $2}')
   NB_CLIENTS=$(($NB_CLIENTS+$nbc))
   NB_CLIENT_MACHINES=$(($NB_CLIENT_MACHINES+1))
done < $CLIENTS_FILE
NB_FAULTY_CLIENTS=$(($NB_CLIENTS*$PERCENTAGE_C/100))

if [ $NB_FAULTY_CLIENTS -ge $NB_CLIENT_MACHINES ]; then
   NB_FAULTY_PER_MACHINE=$(($NB_FAULTY_CLIENTS / $NB_CLIENT_MACHINES))
   one_faulty_machine=0
else
   one_faulty_machine=1
fi


# 3. Launch the clients
manager_ip=$(echo $MANAGER | awk -F':' '{print $2}')

sum_faulty=0
while read LINE
do
   machine=$(echo $LINE | awk '{print $1}')
   nbc=$(echo $LINE | awk '{print $2}')

   if [ $one_faulty_machine -eq 1 ]; then
      nfaulty=$NB_FAULTY_CLIENTS
      one_faulty_machine=-1
   elif [ $one_faulty_machine -eq 0 ]; then
      nfaulty=$NB_FAULTY_PER_MACHINE
   else
      nfaulty=0
      echo "No more faulty to launch"
  fi

   echo -n "Launching $nbc clients with $nfaulty faulty on $machine ..."
   ssh -n $machine "cd $BASE_DIR; nohup ./launch_clients.sh $manager_ip $nbc $nfaulty &> launch_clients_${machine}.log &"
   sum_faulty=$(($sum_faulty+$nfaulty))

   echo "DONE"
done < $CLIENTS_FILE

echo "The percentage of correct clients is $(echo "100-($sum_faulty*100/$NB_CLIENTS)" | bc -l)"


## # ====================================================
## 
# 4. Launch the manager


manager=$(echo $MANAGER | awk -F':' '{print $1}')
ssh -n $manager "cd $BASE_DIR; nohup ./launch_manager.sh $NB_CLIENTS $BURST_SIZE $WARMUP_PHASE $TOTAL_REQS $REQ_SIZE"


# ====================================================

## # 5. Killall & get results


OUTDIR="rrbft_${NB_CLIENTS}clients_${REQ_SIZE}B_perc_faulty_clients_${PERCENTAGE_C}_$(date +%F_%H_%M)"
call_replica_sig_handler
sleep 20
get_results $OUTDIR

