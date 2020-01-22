# List of machines

# The lines are:
#  -for the replicas: hostname:ip_addr
#  -for the clients: hostname:ip_addr:nb_clients
#  -for the manager: hostname:ip_addr


################ 4 machines #################
CLIENTS="sci50:192.168.21.50:100
sci70:192.168.21.70:100
sci71:192.168.21.71:100"

# This is the IP address the clients will use to connect to the verifiers
VERIFIERS="sci74:192.168.21.74
sci75:192.168.21.75
sci76:192.168.21.76
sci77:192.168.21.77"

# NOTE THAT PIR0, PIR1, PIR2 should have different order of replicas, in order
# to have a different primary!

PIR0="sci74:192.168.21.74
sci75:192.168.21.75
sci76:192.168.21.76
sci77:192.168.21.77"

PIR1="sci75:192.168.21.75
sci76:192.168.21.76
sci77:192.168.21.77
sci74:192.168.21.74"

PIR2="sci76:192.168.21.76
sci77:192.168.21.77
sci74:192.168.21.74
sci75:192.168.21.75"


##############################################

MANAGER="sci50:192.168.21.50"

# Do not modify this line!!!
NODES=$(for n in $VERIFIERS $CLIENTS $MANAGER; do echo $n | awk -F ':' '{print $1}'; done | sort | uniq)

