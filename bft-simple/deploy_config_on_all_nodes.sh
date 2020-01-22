#!/bin/sh
#
# $1: optionnal, the ssh key file to be used


# $1: host
# $2: optionnal, ssh key
function scp_config {
if [ $1 != $(hostname) ]; then
   echo -n "Copying config and machines.sh files to $1 ..."

   scp -rp machines.sh $1:$BASE_DIR > /dev/null
   scp -rp scis/config* $1:$BASE_DIR/scis > /dev/null

   echo "DONE"
fi
}


source util.sh

for node in $NODES; do
   scp_config $node $@
done
