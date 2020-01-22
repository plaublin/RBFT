#!/bin/bash

# RRBFT
# static load, 2kB to 100B, worst attack 1
# dynamic load, fault-free + attack 1, 3kB to 100B

while [ true ]; do

# static load
for S in 8; do
    ./launch_xp_fault_free_openloop.sh CLIENTS_${S}B ${S}
    ./launch_xp_attack1_openloop.sh CLIENTS_${S}B ${S} 0
    ./launch_xp_node_flooding_openloop.sh CLIENTS_${S}B ${S}
done

## dynamic load
#for S in 100 500 1000 2000 3000; do
#for S in 3000; do
#
#   # construct the file
#   echo $S > dynamic_workload.txt
#   echo '1 10000' >> dynamic_workload.txt
#
#   if [ $S -eq 100 ]; then
#      echo '5 10000' >> dynamic_workload.txt
#      echo '10 10000' >> dynamic_workload.txt
#      echo '30 10000' >> dynamic_workload.txt
#      echo '10 10000' >> dynamic_workload.txt
#   elif [ $S -eq 500 ]; then
#      echo '5 10000' >> dynamic_workload.txt
#      echo '10 10000' >> dynamic_workload.txt
#      echo '15 10000' >> dynamic_workload.txt
#      echo '10 10000' >> dynamic_workload.txt
#  elif [ $S -eq 1000 ]; then
#      echo '5 10000' >> dynamic_workload.txt
#      echo '10 10000' >> dynamic_workload.txt
#      echo '12 10000' >> dynamic_workload.txt
#      echo '5 10000' >> dynamic_workload.txt
#  elif [ $S -eq 2000 ]; then
#      echo '5 10000' >> dynamic_workload.txt
#      echo '10 10000' >> dynamic_workload.txt
#      echo '12 10000' >> dynamic_workload.txt
#      echo '5 10000' >> dynamic_workload.txt
#  elif [ $S -eq 3000 ]; then
#      echo '3 10000' >> dynamic_workload.txt
#      echo '5 10000' >> dynamic_workload.txt
#      echo '9 10000' >> dynamic_workload.txt
#      echo '3 10000' >> dynamic_workload.txt
#  fi
#
#  echo '1 10000' >> dynamic_workload.txt
#
#  # run the experiment: fault-free
#  #./launch_xp_dynamic_workload_openloop.sh CLIENTS_${S}B ${S} dynamic_workload.txt
#
#  # run the experiment: flooding (to emulate worst attack1)
#  ./launch_xp_dynamic_workload_openloop.sh CLIENTS_${S}B ${S} dynamic_workload.txt NODE_FLOODING 1
#
#done

done
