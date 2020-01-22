#!/bin/bash

# launch an experiment where the workload is dynamic.
# The manager knows (thx to the xp file) who has to send what, and when.
# Note that we do not check if the total number of clients in CLIENTS is greater or equal than
# The number of clients for your experiment (in xp file).
#   $1: file which contains a list of client machines and the number of clients to run on it 
#   $2: req size (also equal to the rep size)
#   $3: xp file for the manager
#   $4: attack. Optionnal
#   $5: arguments for the attack. Required if an attack was specified


if [ $# -eq 5 ] ; then
    CLIENTS_FILE=$1
    REQ_SIZE=$2
    XP_FILE=$3
    ATTACK="$4"
    ATTACK_ARGS="$5"

elif [ $# -eq 3 ] ; then
    CLIENTS_FILE=$1
    REQ_SIZE=$2
    XP_FILE=$3
    ATTACK="NO_ATTACK"

else
    echo "Usage ./$(basename $0) <clients_file> <req_size> <xp_file> [<attack> <args>]"
    echo "attack = PP_DELAY, args = the delay by which PP are delayed, in ms"
    echo "attack = FAULTY_CLIENT_SENDS_VALID_REQ_TO_ONE_NODE, args = proporption of requests sent to all nodes"
    exit 1
fi

# Source this file to get the stop function used below.
source util.sh

# 0. Stop all machines and setup the variables.
./sedinplace.sh DYNAMIC_WORKLOAD_700_PROTOCOLS


PP_DELAY=0
VERIFIER_PIRS_ARGS=""
PROPORTION_GOOD_REQUESTS=1.0

if [ $ATTACK == "PP_DELAY" ];then
   PP_DELAY="$ATTACK_ARGS"
   VERIFIER_PIRS_ARGS="PIR_PP_DELAY $PP_DELAY"

elif [ $ATTACK == "FAULTY_CLIENT_SENDS_VALID_REQ_TO_ONE_NODE" ]; then
   PROPORTION_GOOD_REQUESTS="$ATTACK_ARGS"
   ./sedinplace.sh FAULTY_CLIENT_SENDS_VALID_REQ_TO_ONE_NODE

fi


# ====================================================

stop_all_nodes_without_svn_up

# ====================================================

# 1. Launch the Verifier and the PIRs.
for replica in $VERIFIERS; do
    machine=$(echo $replica | awk -F':' '{print $1}')
    echo -n "Replica $machine ..."
    ssh -n $machine "cd $BASE_DIR; nohup ./launch_verifier_pirs.sh $VERIFIER_PIRS_ARGS &> launch_verifier_pirs.log &"
    echo "DONE"
done

# ====================================================

# 2. Launch the clients
manager_ip=$(echo $MANAGER | awk -F':' '{print $2}')

NB_CLIENTS=0
while read LINE
do
   machine=$(echo $LINE | awk '{print $1}')
   nbc=$(echo $LINE | awk '{print $2}')

   echo -n "Launching $nbc clients on $machine ..."
   ssh -n $machine "cd $BASE_DIR; nohup ./launch_clients_dynamic_workload.sh $manager_ip $nbc $PROPORTION_GOOD_REQUESTS &> launch_clients_${machine}.log &"

   echo "DONE"

   NB_CLIENTS=$(($NB_CLIENTS+$nbc))
done < $CLIENTS_FILE


## # ====================================================
## 
# 4. Launch the manager
manager=$(echo $MANAGER | awk -F':' '{print $1}')
ssh -n $manager "cd $BASE_DIR; nohup ./launch_manager_dynamic_workload.sh $NB_CLIENTS $XP_FILE"

# ====================================================


call_replica_sig_handler
sleep 30

## # 5. Killall & get results
if [ $ATTACK == "PP_DELAY" ];then
   OUTDIR="rrbft_${REQ_SIZE}B_${NB_CLIENTS}clients_dynamic_workload_attack_pp_delay_${PP_DELAY}_$(date +%F_%H_%M)"

elif [ $ATTACK == "FAULTY_CLIENT_SENDS_VALID_REQ_TO_ONE_NODE" ]; then
   OUTDIR="rrbft_${REQ_SIZE}B_${NB_CLIENTS}clients_dynamic_workload_attack_proportion_requests_sent_to_all_nodes_${PROPORTION_GOOD_REQUESTS}_$(date +%F_%H_%M)"

else
   OUTDIR="rrbft_${REQ_SIZE}B_${NB_CLIENTS}clients_dynamic_workload_$(date +%F_%H_%M)"
fi

get_results $OUTDIR
cp $XP_FILE $OUTDIR/

