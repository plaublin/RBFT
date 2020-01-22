#!/bin/bash
#
# This script modifies a kernel parameter 
# This script must be called by root
# Args:
#   $1: value
#   $2: file in /proc (full path)

if [ $# -eq 2 ]; then
   echo "$1" > "$2"
else
   echo "Usage: ./$(basename $0) <value> </proc/file/to/modify>"
fi
