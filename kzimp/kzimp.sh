#!/bin/bash
# Loads kzimp device driver

DEVICE="kzimp"
DEFAULT_NB_FILES=4
OWNER="root"
GROUP="root"
MODE=666

if [ $# -eq 1 ]; then
   CMD=$1
   OPTIONS=""
elif [ $# -ge 2 ]; then
   CMD=$1
   shift
   OPTIONS=$@
else
   echo "Usage: ./$(basename $0) {load|unload|reload} [options...]"
   exit 0
fi

# Am I root and is there sudo on this machine?
if [ ! $(whoami) == "root" ] && [ $(which sudo) ]; then
   SUDO=sudo
else
   SUDO=""
fi


function device_specific_post_load () {
true; # fill at will
}

function device_specific_pre_unload () {
true; # fill at will
}

# $1 is the number of files to create
function create_files {
if [ $# -eq 1 ]; then
   NB_FILES=$1
else
   NB_FILES=$DEFAULT_NB_FILES
fi

i=0
while [ $i -lt ${NB_FILES} ]; do
   file=/dev/${DEVICE}$i

   $SUDO mknod ${file} c $MAJOR $i
   $SUDO chown $OWNER ${file}
   $SUDO chgrp $GROUP ${file}
   $SUDO chmod $MODE ${file}

   i=$(($i+1))
done
}

function remove_files {
$SUDO rm -f /dev/${DEVICE}*
}

# Load and create files
function load_device () {
# Because of Linux first-touch policy, the taskset ensures
# that the memory will be allocated on the node of core 0.
taskset -c 0 $SUDO insmod ./${DEVICE}.ko $OPTIONS
if [ $? -eq 0 ]; then
   MAJOR=`awk "\\$2==\"$DEVICE\" {print \\$1}" /proc/devices`

   if [ $# -eq 1 ]; then
      nb_max_communication_channels=$(echo $OPTIONS | sed 's/.*nb_max_communication_channels=\([[:digit:]]\+\).*/\1/' 2> /dev/null)
      create_files ${nb_max_communication_channels}
   fi

   device_specific_post_load
else
   echo " FAILED!"
   echo "$ lsmod | grep ${DEVICE}"
   lsmod | grep ${DEVICE}
   echo "$ cat /proc/${DEVICE}"
   cat /proc/${DEVICE}
   echo "$ ps -A"
   ps -A
   exit 1
fi
}


# Unload and remove files
function unload_device () {
lsmod | grep ${DEVICE} &>/dev/null
if [ $? -eq 0 ]; then
   device_specific_pre_unload 
   $SUDO rmmod $DEVICE
   if [ $# -eq 1 ]; then
      remove_files $FILES
   fi
else
   echo " Module not loaded. ABORT"
   exit 1
fi
}

# print the status of the module: is it loaded or not?
function get_status {
grep kzimp /proc/modules &> /dev/null
if [ $? -eq 0 ]; then
   STATUS="LOADED"
else
   STATUS="NOT LOADED"
fi

echo " ${STATUS}"
}

case "$CMD" in
   load)
      echo -n "Loading ${DEVICE}..."
      load_device 1
      echo " OK"
      ;;
   unload)
      echo -n "Unloading ${DEVICE}..."
      unload_device 1
      echo " OK"
      ;;
   reload)
      echo -n "Reloading ${DEVICE}..."
      unload_device
      load_device
      echo " OK"
      ;;
   status)
      echo -n "Status ${DEVICE}..."
      get_status
      ;;
   *)
      echo "Cmd: {load|unload|reload|status}"
      exit 1
esac

exit 0
