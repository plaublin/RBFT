#!/bin/bash

# script used to stop the nodes and sar that are launched on this machine (only)

pkill -9 bft_manager &> /dev/null
pkill -9 manager_dyn &> /dev/null
pkill -9 bft_client &> /dev/null
pkill -9 client_openloop &> /dev/null
pkill -9 client_trace_fl &> /dev/null
pkill -9 client_dyn &> /dev/null
pkill -9 bft_pir &> /dev/null
pkill -9 bft_verifier &> /dev/null
pkill -9 sar &> /dev/null
