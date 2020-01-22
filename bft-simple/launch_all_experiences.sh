#!/bin/bash

STATUS_LOG="status_experiments.log.txt"

# Valid values for EXP:
#   FAULT_FREE => no attack
#   PIR_PP_DELAY => The primary of protocol instance 0 delays PP
#   CLIENTS_PROPORTION_G => clients send a proportion g of faithful requests
#   CLIENT_FLOODING => one client send bad requests. The other send good requests. Note that
#                      the number of clients you specify is the number of correct clients. The
#                      faulty one is automatically added by the script
#   NODE_FLOODING => a node (id=3) floods the network.
EXP="FAULT_FREE"


rm $STATUS_LOG >/dev/null 2>&1

for size in 8 100 500 1000 2000 3000 4000; do

    if [ $size == 4000 ] ; then
        NUM_CLIENTS_ARRAY=( 1 3 5 7 10 15 20 25 30 35 )
        NUM_CLIENTS_ARRAY=( 15 )

        # nb clients for clients proportion g
#        if [ $g -eq 10 ]; then
#            NUM_CLIENTS_ARRAY=( 10 )
#        elif [ $g -eq 30 ]; then
#            NUM_CLIENTS_ARRAY=( 16 )
#        elif [ $g -eq 50 ]; then
#            NUM_CLIENTS_ARRAY=( 20 )
#        elif [ $g -eq 70 ]; then
#            NUM_CLIENTS_ARRAY=( 17 )
#        elif [ $g -eq 90 ]; then
#            NUM_CLIENTS_ARRAY=( 16 )
#        fi

    elif [ $size == 3000 ]; then
        NUM_CLIENTS_ARRAY=( 1 3 5 7 10 15 20 25 30 35 40 45 50 )
        NUM_CLIENTS_ARRAY=( 30 )

    elif [ $size == 2000 ]; then
        NUM_CLIENTS_ARRAY=( 1 3 5 7 10 20 25 30 35 40 50 60 )
        NUM_CLIENTS_ARRAY=( 30 )

    elif [ $size == 1000 ]; then
        NUM_CLIENTS_ARRAY=( 1 10 15 30 40 50 60 70 80 )
        NUM_CLIENTS_ARRAY=( 30 )

    elif [ $size == 500 ]; then
        NUM_CLIENTS_ARRAY=( 1 10 15 30 45 55 70 80 90 100 )
        NUM_CLIENTS_ARRAY=( 35 )

    elif [ $size == 100 ]; then
        NUM_CLIENTS_ARRAY=( 1 10 15 30 45 60 80 90 95 100 105 )
        NUM_CLIENTS_ARRAY=( 50 )

    elif [ $size == 8 ]; then
        NUM_CLIENTS_ARRAY=( 1 10 25 50 60 75 80 85 90 95 100 110 )
        NUM_CLIENTS_ARRAY=( 70 )

        # nb clients for clients proportion g
#        if [ $g -eq 10 ]; then
#            NUM_CLIENTS_ARRAY=( 150 )
#        elif [ $g -eq 30 ]; then
#            NUM_CLIENTS_ARRAY=( 150 )
#        elif [ $g -eq 50 ]; then
#            NUM_CLIENTS_ARRAY=( 200 )
#        elif [ $g -eq 70 ]; then
#            NUM_CLIENTS_ARRAY=( 470 )
#        elif [ $g -eq 90 ]; then
#            NUM_CLIENTS_ARRAY=( 550 )
#        fi

    fi

    for num_clients in ${NUM_CLIENTS_ARRAY[@]} ; do

        if [ "$EXP" = "CLIENT_FLOODING" ]; then
            nc=$(( $num_clients / 2 ))
            nc_mod=$(( $num_clients % 2 ))
            nc_sci70=$(( $nc + $nc_mod ))
            echo -e "sci71\t0" > CLIENTS
            echo -e "sci70\t$nc_sci70" >> CLIENTS
            echo -e "sci50\t$nc" >> CLIENTS
        else
            nc=$(( $num_clients / 3 ))
            nc_mod=$(( $num_clients % 3 ))
            nc_sci70=$(( $nc + $nc_mod ))
            echo -e "sci70\t$nc_sci70" > CLIENTS
            if [ $nc -gt 0 ]; then
                echo -e "sci51\t$nc" >> CLIENTS
                echo -e "sci50\t$nc" >> CLIENTS
            fi
        fi

        cat CLIENTS
        echo "====================="

        echo "Starting size=${size}, sci50=${sci50_num_clients}, sci73=${sci73_num_clients}, multicast=$multicast, client_multicast=$client_multicast" >> $STATUS_LOG

        # FAULT FREE
        if [ "$EXP" = "FAULT_FREE" ]; then
            ./launch_xp_fault_free.sh CLIENTS $size
#            ./launch_xp_fault_free_openloop.sh CLIENTS $size
#             ./launch_xp_attack1_openloop.sh CLIENTS $size 0
        fi

        # PIR PP DELAY
        if [ "$EXP" = "PIR_PP_DELAY" ]; then
            for pp_delay in 1 10 100; do 
                ./launch_xp_pir_pp_delay.sh CLIENTS $size $pp_delay
            done
        fi

        # CLIENTS PROPORTION G
        if [ "$EXP" = "CLIENTS_PROPORTION_G" ]; then
            for g in 10 30 50 70 90; do 
                ./launch_xp_clients_proportion_g.sh CLIENTS $size $g
            done
        fi

        # CLIENT FLOODING -- only 1 client send bad requests.
        if [ "$EXP" = "CLIENT_FLOODING" ]; then
                ./launch_xp_client_flooding.sh CLIENTS $size
        fi

        # NODE FLOODING
        if [ "$EXP" = "NODE_FLOODING" ]; then
            ./launch_xp_node_flooding.sh CLIENTS $size
        fi

        echo "-- Finished @ " `date` >> $STATUS_LOG

    done # end for num_clients
done # end for size

