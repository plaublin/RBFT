#!/bin/bash

# sed --in-place of parameters.h for the different macros
# $1: what attack?

FILE_TO_SED_VERIFIER="../libbyz.verifier/parameters.h"
FILE_TO_SED_PIR="../libbyz.pir/parameters.h"
FILE_TO_SED_BENCH="simple_benchmark.h"

# undef the macro $1 in $2
function sed_undef {
   if [ $# -ne 2 ]; then
      echo "sed_undef must be called with 2 args"
      return
   fi

   sed -i "s/#define $1/#undef $1/" $2
}

# define the macro $1 in $2
function sed_define {
   if [ $# -ne 2 ]; then
      echo "sed_define must be called with 2 args"
      return
   fi

   sed -i "s/#undef $1/#define $1/" $2
}


if [ $# -ne 1 ]; then
   echo "./$(basename $0) <attack>"
   exit 0
fi

ATTACK="$1"

if [ $ATTACK == "ATTACK1" ]; then
   sed_define "PERIODICALLY_MEASURE_THROUGHPUT" $FILE_TO_SED_VERIFIER
   sed_undef "MANAGER_FOR_FLUCTUATING_LOAD_TRACE" $FILE_TO_SED_BENCH
   sed_undef "FAULTY_SENDS_VALID_REQ_TO_ONE_NODE" $FILE_TO_SED_VERIFIER
   sed_undef "FAULTY_FOR_ALL" $FILE_TO_SED_VERIFIER
   sed_define "SAVE_LATENCIES" $FILE_TO_SED_BENCH
   sed_define "N_LE_GT_N_F_1" $FILE_TO_SED_PIR
   sed_define "ATTACK1" $FILE_TO_SED_VERIFIER
   sed_define "ATTACK1" $FILE_TO_SED_PIR
   sed_define "ATTACK1" $FILE_TO_SED_BENCH
   exit 0
else
   sed_undef "ATTACK1" $FILE_TO_SED_VERIFIER
   sed_undef "ATTACK1" $FILE_TO_SED_PIR
   sed_undef "ATTACK1" $FILE_TO_SED_BENCH
fi


if [ $ATTACK == "ATTACK2" ]; then
   sed_define "PERIODICALLY_MEASURE_THROUGHPUT" $FILE_TO_SED_VERIFIER
   sed_undef "MANAGER_FOR_FLUCTUATING_LOAD_TRACE" $FILE_TO_SED_BENCH
   sed_undef "FAULTY_SENDS_VALID_REQ_TO_ONE_NODE" $FILE_TO_SED_VERIFIER
   sed_undef "FAULTY_FOR_ALL" $FILE_TO_SED_VERIFIER
   sed_define "SAVE_LATENCIES" $FILE_TO_SED_BENCH
   sed_define "N_LE_GT_N_F_1" $FILE_TO_SED_PIR
   sed_define "ATTACK2" $FILE_TO_SED_VERIFIER
   sed_define "ATTACK2" $FILE_TO_SED_PIR
   sed_define "ATTACK2" $FILE_TO_SED_BENCH
   exit 0
else
   sed_undef "ATTACK2" $FILE_TO_SED_VERIFIER
   sed_undef "ATTACK2" $FILE_TO_SED_PIR
   sed_undef "ATTACK2" $FILE_TO_SED_BENCH
fi

if [ $ATTACK == "FAULTY_CLIENT_SENDS_VALID_REQ_TO_ONE_NODE" ]; then
   sed_define "FAULTY_SENDS_VALID_REQ_TO_ONE_NODE" $FILE_TO_SED_VERIFIER
   sed_undef "FAULTY_FOR_ALL" $FILE_TO_SED_VERIFIER
   #Note: sedinplace.sh must be called the 2nd time with FAULTY_SENDS_VALID_REQ_TO_ONE_NODE
   exit 0
else
   sed_undef "FAULTY_SENDS_VALID_REQ_TO_ONE_NODE" $FILE_TO_SED_VERIFIER
   sed_define "FAULTY_FOR_ALL" $FILE_TO_SED_VERIFIER
fi

if [ $ATTACK == "NODE_FLOODING" ]; then
   sed_define "N_LE_GT_N_F_1" $FILE_TO_SED_PIR
else
   sed_undef "N_LE_GT_N_F_1" $FILE_TO_SED_PIR
fi

if [ $ATTACK == "FLUCTUATING_LOAD_TRACE" ]; then
   sed_define "PERIODICALLY_MEASURE_THROUGHPUT" $FILE_TO_SED_VERIFIER
   sed_define "MANAGER_FOR_FLUCTUATING_LOAD_TRACE" $FILE_TO_SED_BENCH
   sed_define "SAVE_LATENCIES" $FILE_TO_SED_BENCH

elif [ $ATTACK == "DYNAMIC_WORKLOAD_700_PROTOCOLS" ]; then
   sed_define "PERIODICALLY_MEASURE_THROUGHPUT" $FILE_TO_SED_VERIFIER
   sed_undef "MANAGER_FOR_FLUCTUATING_LOAD_TRACE" $FILE_TO_SED_BENCH
   sed_define "SAVE_LATENCIES" $FILE_TO_SED_BENCH

elif [ $ATTACK == "DYNAMIC_WORKLOAD_700_PROTOCOLS_NODE_FLOODING" ]; then
   sed_define "PERIODICALLY_MEASURE_THROUGHPUT" $FILE_TO_SED_VERIFIER
   sed_undef "MANAGER_FOR_FLUCTUATING_LOAD_TRACE" $FILE_TO_SED_BENCH
   sed_define "SAVE_LATENCIES" $FILE_TO_SED_BENCH
   sed_define "N_LE_GT_N_F_1" $FILE_TO_SED_PIR

else 
   sed_undef "PERIODICALLY_MEASURE_THROUGHPUT" $FILE_TO_SED_VERIFIER
   sed_undef "MANAGER_FOR_FLUCTUATING_LOAD_TRACE" $FILE_TO_SED_BENCH
   sed_undef "SAVE_LATENCIES" $FILE_TO_SED_BENCH
fi

if [ $ATTACK == "PIR_PP_DELAY_OPENLOOP" ]; then
  sed_define "LIMIT_REQ_LIST_USAGE" $FILE_TO_SED_PIR
else
  sed_undef "LIMIT_REQ_LIST_USAGE" $FILE_TO_SED_PIR
fi

