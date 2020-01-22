#!/bin/bash

# List of machines
source machines.sh

# number of tolerated faults
TOLERATED_FAULTS=2
nb_replicas=$((3 * $TOLERATED_FAULTS + 1))
nb_replicas2=0
for replica in $VERIFIERS; do
   nb_replicas2=$(($nb_replicas2 + 1))
done

if [ $nb_replicas != $nb_replicas2 ]; then
   echo "The number of tolerated faults is not consistent with the specified number of replicas. Check the list of machines."
fi

# total number of nodes (replicas + clients)
nb_replicas=$((3 * $TOLERATED_FAULTS + 1))
nb_clients=0
for client in $CLIENTS; do
   nb_tmp=$(echo $client |  tr ':' ' ' | awk '{print $3}')
   nb_clients=$(($nb_clients + nb_tmp))
done

NB_NODES=$(($nb_replicas + $nb_clients))

# starting port for replicas
INIT_REPLICA_PORT=3660

# starting port for clients
INIT_CLIENT_PORT=$(($INIT_REPLICA_PORT + $nb_replicas))

# starting port for PIRs
INIT_PIR_PORT=6000

# public key
PUBLIC_KEY="bfaa873efc926cb91646a89e45f96582041e3eed35cde0ef60b5c006cfad883781ee807411b0df3c74dc3ebbbce59c21d67711c83ecf596357c23dba33da338fb5577179a3b6188c59590aa1301eb852c0e14fa9225c0b377fee944eb9fa110ad7a316269e4b13b153887426a347c7c3c5feb1e3107bac4c6e29327b3343c405"

# service name
SERVICE_NAME="generic"

# authentication timeout
AUTH_TIMEOUT=1800000


# multicast IP address, not used by Aardvark
MULTICAST_ADDR="234.5.6.8 3000"

# replica view change timeout
VIEW_CHANGE_TIMEOUT=50000

# replica status timeout
STATUS_TIMEOUT=100

# replica recovery timeout
RECOVERY_TIMEOUT=9999250000


########################################
# config.client
########################################

# name of this config file
OUT="scis/config.clients"
echo "Generating $OUT"

## 1) header ##
echo $SERVICE_NAME > $OUT
echo $TOLERATED_FAULTS >> $OUT
echo $AUTH_TIMEOUT >> $OUT
echo $NB_NODES >> $OUT
echo $MULTICAST_ADDR >> $OUT


## 2) list of nodes ##

REPLICA_PORT=$INIT_REPLICA_PORT
for replica in $VERIFIERS; do
   machine=$(echo $replica |  tr ':' ' ' | awk '{print $1}')
   ip_addr=$(echo $replica |  tr ':' ' ' | awk '{print $2}')

   echo $machine $ip_addr $REPLICA_PORT $PUBLIC_KEY >> $OUT
   REPLICA_PORT=$(($REPLICA_PORT+1))
done

# not necessary since we increment client port upper
CLIENT_PORT=$INIT_CLIENT_PORT
for client in $CLIENTS; do
   machine=$(echo $client |  tr ':' ' ' | awk '{print $1}')
   ip_addr=$(echo $client |  tr ':' ' ' | awk '{print $2}')
   number=$(echo $client |  tr ':' ' ' | awk '{print $3}')

   #CLIENT_PORT=$INIT_CLIENT_PORT
   for n in $(seq 1 $number); do
      echo $machine $ip_addr $CLIENT_PORT $PUBLIC_KEY >> $OUT
      CLIENT_PORT=$(($CLIENT_PORT+1))
   done
done


## 3) footer ##

echo $VIEW_CHANGE_TIMEOUT >> $OUT
echo $STATUS_TIMEOUT >> $OUT
echo $RECOVERY_TIMEOUT >> $OUT


#########################################

echo "!!! NOTE: this script assumes machines.sh specifies 3 PIRs configurations !!!"

########################################
# config.pi0

i=0
OUT="scis/config.pi${i}"
echo "Generating $OUT"

## 1) header ##
echo "protocolInstance${i}" > $OUT
echo $TOLERATED_FAULTS >> $OUT
echo $AUTH_TIMEOUT >> $OUT
echo $NB_NODES >> $OUT
echo $MULTICAST_ADDR >> $OUT

## 2) list of nodes ##

REPLICA_PORT=$INIT_PIR_PORT
for replica in ${PIR0}; do
   machine=$(echo $replica |  tr ':' ' ' | awk '{print $1}')
   ip_addr=$(echo $replica |  tr ':' ' ' | awk '{print $2}')

   echo $machine $ip_addr $REPLICA_PORT $PUBLIC_KEY >> $OUT
done

# not necessary since we increment client port upper
CLIENT_PORT=$INIT_CLIENT_PORT
for client in $CLIENTS; do
   machine=$(echo $client |  tr ':' ' ' | awk '{print $1}')
   ip_addr=$(echo $client |  tr ':' ' ' | awk '{print $2}')
   number=$(echo $client |  tr ':' ' ' | awk '{print $3}')

   #CLIENT_PORT=$INIT_CLIENT_PORT
   for n in $(seq 1 $number); do
      echo $machine $ip_addr $CLIENT_PORT $PUBLIC_KEY >> $OUT
      CLIENT_PORT=$(($CLIENT_PORT+1))
   done
done


## 3) footer ##

echo $i >> $OUT
echo $VIEW_CHANGE_TIMEOUT >> $OUT
echo $STATUS_TIMEOUT >> $OUT
echo $RECOVERY_TIMEOUT >> $OUT


########################################
# config.pi1

i=1
OUT="scis/config.pi${i}"
echo "Generating $OUT"

## 1) header ##
echo "protocolInstance${i}" > $OUT
echo $TOLERATED_FAULTS >> $OUT
echo $AUTH_TIMEOUT >> $OUT
echo $NB_NODES >> $OUT
echo $MULTICAST_ADDR >> $OUT

## 2) list of nodes ##

REPLICA_PORT=$INIT_PIR_PORT
for replica in ${PIR1}; do
   machine=$(echo $replica |  tr ':' ' ' | awk '{print $1}')
   ip_addr=$(echo $replica |  tr ':' ' ' | awk '{print $2}')

   echo $machine $ip_addr $REPLICA_PORT $PUBLIC_KEY >> $OUT
done

# not necessary since we increment client port upper
CLIENT_PORT=$INIT_CLIENT_PORT
for client in $CLIENTS; do
   machine=$(echo $client |  tr ':' ' ' | awk '{print $1}')
   ip_addr=$(echo $client |  tr ':' ' ' | awk '{print $2}')
   number=$(echo $client |  tr ':' ' ' | awk '{print $3}')

   #CLIENT_PORT=$INIT_CLIENT_PORT
   for n in $(seq 1 $number); do
      echo $machine $ip_addr $CLIENT_PORT $PUBLIC_KEY >> $OUT
      CLIENT_PORT=$(($CLIENT_PORT+1))
   done
done


## 3) footer ##

echo $i >> $OUT
echo $VIEW_CHANGE_TIMEOUT >> $OUT
echo $STATUS_TIMEOUT >> $OUT
echo $RECOVERY_TIMEOUT >> $OUT


########################################
# config.pi2

i=2
OUT="scis/config.pi${i}"
echo "Generating $OUT"

## 1) header ##
echo "protocolInstance${i}" > $OUT
echo $TOLERATED_FAULTS >> $OUT
echo $AUTH_TIMEOUT >> $OUT
echo $NB_NODES >> $OUT
echo $MULTICAST_ADDR >> $OUT

## 2) list of nodes ##

REPLICA_PORT=$(( $INIT_PIR_PORT))
for replica in ${PIR2}; do
   machine=$(echo $replica |  tr ':' ' ' | awk '{print $1}')
   ip_addr=$(echo $replica |  tr ':' ' ' | awk '{print $2}')

   echo $machine $ip_addr $REPLICA_PORT $PUBLIC_KEY >> $OUT
done

# not necessary since we increment client port upper
CLIENT_PORT=$INIT_CLIENT_PORT
for client in $CLIENTS; do
   machine=$(echo $client |  tr ':' ' ' | awk '{print $1}')
   ip_addr=$(echo $client |  tr ':' ' ' | awk '{print $2}')
   number=$(echo $client |  tr ':' ' ' | awk '{print $3}')

   #CLIENT_PORT=$INIT_CLIENT_PORT
   for n in $(seq 1 $number); do
      echo $machine $ip_addr $CLIENT_PORT $PUBLIC_KEY >> $OUT
      CLIENT_PORT=$(($CLIENT_PORT+1))
   done
done


## 3) footer ##

echo $i >> $OUT
echo $VIEW_CHANGE_TIMEOUT >> $OUT
echo $STATUS_TIMEOUT >> $OUT
echo $RECOVERY_TIMEOUT >> $OUT


