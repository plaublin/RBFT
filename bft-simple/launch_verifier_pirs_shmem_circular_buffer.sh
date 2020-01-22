#!/bin/bash

# script used to launch a verifier 2 sar (1 for CPU, 1 for NET) and N PIRs
# note: you need to redirect the output of the verifier to a file by yourself
# note: core association is terminator specific
# $1: path to the config files, without extension
# $2: number of PIRs


# This is the default in the launcher script.
echo "Using default config: 'scis/config.clients' and 'scis/config.pi<0,1,2>' for 3 PIR instances"
CONFIG="scis/config"
NB_PIRS=3

NB_THREADS_PER_CORE=1

CONFIG_PRIV="config_private/template"
# launch sar during at most 1 day
DURATION=$((24*60*60))

#taskset -c 6 sar -n DEV 1 $DURATION > net_replica_$(hostname).log &
#taskset -c 7 sar -P ALL 1 $DURATION > cpu_replica_$(hostname).log &

ulimit -c unlimited

# shared memory segments specific
# clean stuff of previous run
./remove_shared_segment.pl
for i in $(seq 0 $(($NB_PIRS-1))); do
   rm -f /tmp/ftok_pir_verifier_$i
   rm -f /tmp/ftok_verifier_pir_$i
done

# used by ftok
for i in $(seq 0 $(($NB_PIRS-1))); do
   touch /tmp/ftok_pir_verifier_$i
   touch /tmp/ftok_verifier_pir_$i
done

#set new parameters
sudo ./root_set_value.sh 16000000000 /proc/sys/kernel/shmall
sudo ./root_set_value.sh 16000000000 /proc/sys/kernel/shmmax


nbc=1
while [ $nbc -gt 0 ]; do
   echo "Waiting for the end of TIME_WAIT connections before launching verifier"
   sleep 20
   nbc=$(netstat -tn | grep TIME_WAIT | grep -v ":22 " | wc -l)
done

for i in $(seq 0 $(($NB_PIRS-1))); do
   core=$(($(($i+3)) * NB_THREADS_PER_CORE))
#   taskset -c $core ./bft_pir -c ${CONFIG}.pi${i} -p $CONFIG_PRIV &> pir_$(hostname)_$i.log &

   ########### GDB ##########
   echo "run -c ${CONFIG}.pi${i} -p $CONFIG_PRIV" > /tmp/batch_pir${i}.gdb
   echo "bt" >> /tmp/batch_pir${i}.gdb
   echo "quit" >> /tmp/batch_pir${i}.gdb
   taskset -c $core gdb -x /tmp/batch_pir${i}.gdb ./bft_pir &> pir_$(hostname)_$i.log &
done

# the PIRs create the shared memory segments for the circular buffer between the Verifier and the PIRs
# this sleep ensures they have been created
sleep 1

nbc=1
while [ $nbc -gt 0 ]; do
   echo "Waiting for the end of TIME_WAIT connections before launching verifier"
   sleep 20
   nbc=$(netstat -tn | grep TIME_WAIT | grep -v ":22 " | wc -l)
done

##taskset -c 0,1,2 ./bft_verifier -c ${CONFIG}.clients -p $CONFIG_PRIV &> verifier_$(hostname).log &
#./bft_verifier -c ${CONFIG}.clients -p $CONFIG_PRIV &> verifier_$(hostname).log &
#valgrind --tool=memcheck --leak-check=full --log-file=valgrind_verfifier_$(hostname).vg ./bft_verifier -c ${CONFIG}.clients -p $CONFIG_PRIV &> verifier_$(hostname).log &

########### GDB ##########
echo "run -c ${CONFIG}.clients -p $CONFIG_PRIV" > /tmp/batch_verifier.gdb
echo "bt" >> /tmp/batch_verifier.gdb
echo "quit" >> /tmp/batch_verifier.gdb
gdb -x /tmp/batch_verifier.gdb ./bft_verifier &> verifier_$(hostname).log &

