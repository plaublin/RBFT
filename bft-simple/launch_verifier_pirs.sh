#!/bin/bash

# script used to launch a verifier 2 sar (1 for CPU, 1 for NET) and N PIRs
# note: you need to redirect the output of the verifier to a file by yourself
# $1: optionnal, if set, it specifies the attack that should be run
# $2: required if $1 is given. It sets the additionnal argument

# Set this flag to GDB if you want to activate gdb
# or VALGRIND if you want to use valgrind
DEBUG="GDB"

PIRS_ARGS=""
VERIFIERS_ARGS=""

if [ $# -ge 1 ]; then
    case $1 in
        PIR_PP_DELAY)
        PIRS_ARGS="-d $2"
        ;;
        NODE_FLOODING)
        PIRS_ARGS="-z"
        VERIFIERS_ARGS="-z"
        ;;
        *)
        echo "Unknown attack $1"
        exit 0
        ;;
    esac
fi

source util.sh

# This is the default in the launcher script.
echo "Using default config: 'scis/config.clients' and 'scis/config.pi<0,1,2>' for 3 PIR instances"
CONFIG="scis/config"
NB_PIRS=$(($NB_FAULTS+1))

CONFIG_PRIV="config_private/template"
# launch sar during at most 1 day
DURATION=$((24*60*60))

taskset -c 6 sar -n DEV 1 $DURATION > net_replica_$(hostname).log &
taskset -c 7 sar -P ALL 1 $DURATION > cpu_replica_$(hostname).log &

ulimit -c unlimited

# kzimp specific
KZIMP_DIR="../kzimp/"

cd $KZIMP_DIR
make
./kzimp.sh unload
./kzimp.sh load nb_max_communication_channels=$((1+$NB_PIRS)) default_channel_size=15000 default_max_msg_size=9000 default_timeout_in_ms=60000
if [ $? -eq 1 ]; then
   echo "An error has occured when loading kzimp. Aborting the experiment"
   exit 0
fi
cd -


nbc=1
while [ $nbc -gt 0 ]; do
   echo "Waiting for the end of TIME_WAIT connections before launching verifier"
   sleep 20
   nbc=$(netstat -tn | grep TIME_WAIT | grep -v ":22 " | wc -l)
done


if [ $NB_FAULTS -eq 1 ]; then
    CONFIG_PIR=${CONFIG}
else
    CONFIG_PIR=${CONFIG}.$(hostname)
fi

for i in $(seq 0 $(($NB_PIRS-1))); do
   core=$(($i+4))

   if [ -z $DEBUG ]; then
       taskset -c $core ./bft_pir -c ${CONFIG_PIR}.pi${i} -p $CONFIG_PRIV ${PIRS_ARGS} &> pir_$(hostname)_$i.log &
   elif [ "$DEBUG" = "GDB" ]; then
       ########### GDB ##########
       echo "handle SIGINT pass" > /tmp/batch_pir${i}.gdb
       echo "handle SIGINT nostop" >> /tmp/batch_pir${i}.gdb
       echo "handle SIGPIPE pass" >> /tmp/batch_pir${i}.gdb
       echo "handle SIGPIPE nostop" >> /tmp/batch_pir${i}.gdb
       echo "run -c ${CONFIG_PIR}.pi${i} -p $CONFIG_PRIV ${PIRS_ARGS}" >> /tmp/batch_pir${i}.gdb
       echo "bt" >> /tmp/batch_pir${i}.gdb
       echo "quit" >> /tmp/batch_pir${i}.gdb
       taskset -c $core gdb -x /tmp/batch_pir${i}.gdb ./bft_pir &> pir_$(hostname)_$i.log &
   elif [ "$DEBUG" = "VALGRIND" ];then
       taskset -c $core valgrind --tool=memcheck --leak-check=full --log-file=valgrind_pir_$(hostname)_$i.log ./bft_pir -c ${CONFIG_PIR}.pi${i} -p $CONFIG_PRIV $PIRGS_ARGS &> pir_$(hostname)_${i}.log &
   else
       echo "Unknown $DEBUG"
   fi
done


if [ $NB_FAULTS -eq 1 ]; then
    CONFIG_VERIFIER=${CONFIG}.clients
else
    CONFIG_VERIFIER=${CONFIG}.$(hostname)
fi

#if [ "$(hostname)" = "sci74" ]; then
#    echo valgrind --tool=memcheck --leak-check=full --show-reachable=yes --track-origins=yes --log-file=valgrind_verifier_$(hostname).log ./bft_verifier -c ${CONFIG_VERIFIER} -p $CONFIG_PRIV $VERIFIERS_ARGS &> verifier_$(hostname).log

#else

if [ -z $DEBUG ]; then
    ./bft_verifier -c ${CONFIG_VERIFIER} -p $CONFIG_PRIV ${VERIFIERS_ARGS} &> verifier_$(hostname).log &
elif [ "$DEBUG" = "GDB" ]; then
    ########### GDB ##########
    echo "handle SIGINT pass" > /tmp/batch_verifier.gdb
    echo "handle SIGINT nostop" >> /tmp/batch_verifier.gdb
    echo "handle SIGPIPE pass" >> /tmp/batch_verifier.gdb
    echo "handle SIGPIPE nostop" >> /tmp/batch_verifier.gdb
    echo "run -c ${CONFIG_VERIFIER} -p $CONFIG_PRIV $VERIFIERS_ARGS" >> /tmp/batch_verifier.gdb
    echo "bt" >> /tmp/batch_verifier.gdb
    echo "quit" >> /tmp/batch_verifier.gdb
    gdb -x /tmp/batch_verifier.gdb ./bft_verifier &> verifier_$(hostname).log &
elif [ "$DEBUG" = "VALGRIND" ];then
    valgrind --tool=memcheck --leak-check=full --show-reachable=yes --log-file=valgrind_verifier_$(hostname).log ./bft_verifier -c ${CONFIG_VERIFIER} -p $CONFIG_PRIV $VERIFIERS_ARGS &> verifier_$(hostname).log &
else
    echo "Unknown $DEBUG"
fi

#fi
