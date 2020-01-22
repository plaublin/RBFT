 # Source this file to get the stop function used below.
source util.sh

# ====================================================
       
# 0. Stop all machines and setup the variables.
 stop_all_nodes_without_svn_up
          
# ====================================================
    
# 1. Launch the Verifier and the PIRs.
for replica in $VERIFIERS; do
machine=$(echo $replica | awk -F':' '{print $1}')
echo -n "Replica $machine ..."
ssh -n $machine "cd $BASE_DIR; nohup ./launch_verifier_pirs.sh &> launch_verifier_pirs.log &"
echo
done

