#!/bin/bash

# script used to launch a PIR
# note: you need to redirect the output of the verifier to a file by yourself
# $1: config file
# $2: core on which this PIR will be executed


if [ $# -ne 1 ]; then
   echo "Args: <config_file>"
   echo "Using config.pi00 this time"
   CONFIG="config.pi00"
else
	if [ $# -eq 2 ]; then
      CONFIG=$1
      CORE=$2
    else
      echo "Args: <config_file> <core>"
    fi
fi

CONFIG_PRIV="config_private/template"

taskset -c $CORE ./pir -c $CONFIG -p $CONFIG_PRIV 2>&1
