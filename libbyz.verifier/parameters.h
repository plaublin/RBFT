#ifndef _parameters_h
#define _parameters_h 1

const int Max_num_replicas = 32;

// Interval in sequence space between "checkpoint" states, i.e.,
// states that are checkpointed and for which Checkpoint messages are
// sent.
const int checkpoint_interval = 128;

// Maximum number of messages for which protocol can be
// simultaneously in progress, i.e., messages with sequence number
// higher than last_stable+max_out are ignored. It is required that
// max_out > checkpoint_interval. Otherwise, the algorithm will be
// unable to make progress.
const int max_out = 256;

/* !!! Note on checkpoint_interval and max_out !!!
 *
 * we would like to use higher values for checkpoint interval (and for max_out)
 * to achieve higher throughput. What prevent us from using 1024 and 2048 as 
 * values (that I remember being default values for zyzzyva... but I may be 
 * wrong) is that at some point in the state transfer we have to biuld a big 
 * message containing a lot of pre_prepares (from the latest stable checkpoint
 * of the replica that needs the state, to the last pre_prepare known by the 
 * other replicas). If the checkpoint interval is too big, this message may 
 * be bigger than MAX_MESSAGE_SIZE, triggering a nice assertion.
 *
 * 128 and 256 are small enough. 
 *
 * I've never tried to increase MAX_MESSAGE_SIZE, that may help.
 * otherwise it should be possible to split a message bigger than 
 * MAX_MESSAGE_SIZE in two or more smaller messages.
 *
 * */

//! JC: This controls batching, and is the maximum number of messages that
//! can be outstanding at any one time. ie. with CW=1, can't send a pp unless previous
//! operation has executed.
const int congestion_window = 1;

/* !!! Note on congestion_window !!!
 *
 * It would really be nice to use a congestion_window higher than one.
 * The main problem is that a congestion window higher than 1 breaks something
 * inside the state transfer black magic, and view change becomes unstable.
 *
 * I've also observed a memory corruption detected by glibc wile messing
 * around with congestion_window = 2, but this may well be caused by our 
 * modified memory allocator. (even though I have no idea why this happens
 * only with congestion_window > 1)
 *
 * 1 is a safe value, everithing else is not.
 *
 * UPDATE: after having implemented shared memory communication among threads,
 * we are able to use congestion windows higher than 1.
 * Don't know why...
 *
 */


/*******************************************************************************/
#define REPLICA_FLOOD_PROTECTION 1 //enable the (soft) protection against replica flood

const int flood_detection_factor = 20;
//trigger flood protection for replica x if messages sent by replica x are > than max(messages sent from other replicas)*flood_protection_factor 

const int check_nb_msg = 2000;
// check for a possible flood every check_rmcount replica messages
/*******************************************************************************/


const int circular_buffer_size = 15000;

const int expected_pre_prepares = 12;
const int pre_prepare_timer_duration = 480;
// trigger a view change if I have not received at least expected_pre_prepares
// pre_prepare messages in the last pre_prepare_timer_duration ms 
// N.B. atfer triggering a view change due to the preprepare timer, its period is doubled
// the period is set back to the pre_prepare_timer_duration if the timer expires without
// triggering a view change

/* --- END DELAY ADAPTIVENESS (aka pre_prepare adaptiveness) ---- */

/* ----  FAIRNESS ADAPTIVENESS  ---- */
#define FAIRNESS_ADAPTIVE 1 //forces a view change if the progress is too unfair
#undef FAIRNESS_ADAPTIVE
const int fairness_multiplier = 2;

/* ---- END FAIRNESS ADAPTIVENESS  ---- */

const int batch_size_limit = 1000;

/* Define SIGN_REQUESTS to force all requests to be signed by the client */
#define SIGN_REQUESTS 
//#define SIMULATE_SIGS

// clients do not sign the requests
#define NO_CLIENT_SIGNATURES


#undef COSTLY_EXECUTION

/* client timeout for retransmission of requests, in ms */
#ifdef COSTLY_EXECUTION
const int client_retrans_timeout = 10000;
#else
const int client_retrans_timeout = 500;
#endif

/* Forwarding thread: retransmission timeout for Propagate in ms */
#define FORWARDER_RETRANS_TIMEOUT 500

/* Forwarding thread: max time between the send of 2 Propagate, in ms */
#define PERIODIC_PROPAGATE_SEND (0.05)

/* TCP port used by the verifier when bootstrapping */
#define VERIFIER_BOOTSTRAP_PORT 7000

/* UDP port on which the verifier thread listens for messages from the clients */
#define VERIFIER_THREAD_LISTENING_PORT 9876

/*
 * TCP port on which the master protocol instance binds itself for
 * communications with the Verifier.
 * The port on which the other instances bind is PIR_VERIFIER_PORT + no_instance,
 * e.g. instance i -> PIR_VERIFIER_PORT + i
 */
#define PIR_VERIFIER_PORT 6020

// define it in order to deactivate the view changes
#define VIEW_CHANGE_DEACTIVATED
#undef VIEW_CHANGE_DEACTIVATED

// define it if you want to deactivate the protocol instance change
#define PROTOCOL_INSTANCE_CHANGE_DEACTIVATED
#undef PROTOCOL_INSTANCE_CHANGE_DEACTIVATED

// When a Protocol Instance Change has occured, do not check the performance during
// a grace period (in number of calls to the timer)
#define MONITORING_GRACE_PERIOD 5

// throughput max acceptable ratio
const double DELTA_THROUGHPUT = -0.5;

// max acceptable latency, in ms
const double MAX_ACCEPTABLE_LATENCY = 15;

// latency max acceptable ratio
const double DELTA_LATENCY = 0.5;

//Faulty clients send bad requests to the node 0 only
//The proportion of bad requests is to be specified when
//launching the script launch_xp_clients_flooding.sh as a percentage
//#define FAULTY_FOR_MASTER

//Faulty clients send bad requests to all the nodes
//The proportion of bad requests is to be specified when
//launching the script launch_xp_clients_flooding.sh as a percentage
#define FAULTY_FOR_ALL

// Faulty clients send a correct request to only 1 node.
// The Propagate phase ensures it is eventually received by all the corret nodes
#undef FAULTY_SENDS_VALID_REQ_TO_ONE_NODE

// size of the circular buffer in shared memory
const int shared_mem_circular_buffer_size = 1000;

// ftok pathnames for the verifier <-> pirs circular buffer in shared memory
//#define FTOK_VERIFIER_TO_PIR "/tmp/ftok_verifier_pir"
//#define FTOK_PIR_TO_VERIFIER "/tmp/ftok_pir_verifier"


// for kzimp
#define KZIMP_CHAR_DEV_FILE "/dev/kzimp"

#define NETWORK_INTERFACE_IP_FOR_FORWARDER 21

// period of the statTimer handler, i.e. the handler that monitors periodically the PIRs, in ms
#define MONITORING_PERIOD 1000

// monitoring log the stats of all the requests for an offline analysis
//#define LOG_LAT_OFFLINE_ANALYSIS
#undef LOG_LAT_OFFLINE_ANALYSIS
#define NB_MAX_LOGGED_REQS 100000

/****************************** BLACKLISTING ******************************/
#define NO_BLACKLISTING
//#undef NO_BLACKLISTING
#define PERIODIC_DISPLAY_PERC_VALID 1000000
#define NODE_BLACKLISTER_SLIDING_WINDOW_SIZE 20
#define READING_FROM_BLACKLISTED_NODES_PERIOD 100


/****************************** WHEN USING A REAL TRACE ******************************/
// Do the replicas periodically measure the throughput?
// What is the period (in ms)?
// What is the max nb of measures?
#undef PERIODICALLY_MEASURE_THROUGHPUT
#define PRINT_THROUGHPUT_PERIOD 1000
#define MAX_NB_MEASURES 1000


// attack 1: the primary of the master instance is correct.
// the attacker wants a protocol instance change to be voted
#undef ATTACK1

// attack 2: the primary of the master instance is malicious
// the attack does not want a protocol instance change to be voted
#undef ATTACK2

// does the forwarding thread balances the load over 2 NICs for its communication?
#define LOAD_BALANCING_SEND
#undef LOAD_BALANCING_SEND

// do we use TCP (or UDP) connections?
#define USE_TCP_CONNECTIONS (1)

const unsigned long long MISSING_RID_MAX_RANGE = max_out;

// period of the pipe thr timer in ms
#define DEBUG_PERIODIC_THR_PERIOD 1000
#define DEBUG_PERIODICALLY_PRINT_THR
#undef DEBUG_PERIODICALLY_PRINT_THR

#endif // _parameters_h
