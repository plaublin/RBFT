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

const int circular_buffer_size = 2000;

/* ---- THROUGHPUT ADAPTIVENESS ---- */

//#define THROUGHPUT_ADAPTIVE 1 //forces a view change if replicas do not observe a satisfactory throughput
#undef THROUGHPUT_ADAPTIVE
//PARAMETERS:

const float req_throughput_init = 0.0001;
//the required throughput is initialized to req_throughout_init reqs/ns.
//e.g. 0.0001 -> 100 req/s

const float req_throughput_increment_init = 0.0001;
//if the observed throughput is higher then the required throughput, the
//required throughput is incremented. this increment is initialized at 
//req_throughout_increment_init reqs/ns. [this value is used only for the first
//check, when we have no measure for the previous throughput]

//initial value: const float req_throughput_increment_scaling_factor = 0.005;
const float req_throughput_increment_scaling_factor = 0.01;
//if the observed throughput is higher than the required throughput,
//the required throughput is incremented by itself*req_throughput_increment_scaling_factor.
//e.g., 0.005 means that the req throughput is incremented by 0.5% every checkpoint,
//until it exceeds the observed throughput 

//initial value: const float new_req_throughput_scaling_factor = 0.80;
const float new_req_throughput_scaling_factor = 0.90;
//after a view change, the required throughput is set as
//new_req_throughput_scaling_factor * (highest of the last throughputs measured for each replica)
//e.g., 0.8 means that the new requirement will be the 80% of the highest throughput
//among the last throughput observed by all the replicas.

//initial value: const int increment_timer_duration = 10000;
const int increment_timer_duration = 5000;
//right after a view change, wait increment_timer_duration ms before starting
//to increment the required throughput

const int throughput_timer_duration = 1;
//1 ms, this timer has to be fast... 
//it is just an interface to call send_view_change
//as soon as it is safe to do so

/* ---- END THROUGHPUT ADAPTIVENESS ---- */

/* ---- DELAY ADAPTIVENESS (aka pre_prepare adaptiveness) ---- */

//#define DELAY_ADAPTIVE 1 // forces a view change if replicas do not receive pre_prepares often enough
//delay adaptiveness is used to guarantee that we reach a checkpoint in a determined interval of time
#undef DELAY_ADAPTIVE

const int expected_pre_prepares = 12;
const int pre_prepare_timer_duration = 480;
// trigger a view change if I have not received at least expected_pre_prepares
// pre_prepare messages in the last pre_prepare_timer_duration ms 
// N.B. atfer triggering a view change due to the preprepare timer, its period is doubled
// the period is set back to the pre_prepare_timer_duration if the timer expires without
// triggering a view change

/* --- END DELAY ADAPTIVENESS (aka pre_prepare adaptiveness) ---- */

/* ----  FAIRNESS ADAPTIVENESS  ---- */
//#define FAIRNESS_ADAPTIVE 1 //forces a view change if the progress is too unfair
#undef FAIRNESS_ADAPTIVE
const int fairness_multiplier = 2;

/* ---- END FAIRNESS ADAPTIVENESS  ---- */

/* ----  REPLICA FLOOD PROTECTION  ---- */

#define REPLICA_FLOOD_PROTECTION 1 //enable the (soft) protection against replica flood

const int flood_detection_factor = 20;
//trigger flood protection for replica x if messages sent by replica x are > than max(messages sent from other replicas)*flood_protection_factor 

const int check_rmcount = 2000;
// check for a possible flood every check_rmcount replica messages

// const float rmcount_threshold = 0.5; no more used
// a flood is detected if one replica is responsible for
// rmcount_threshold*check_rmcount of the last check_rmcount
// messages.
// e.g. 0.6 means that if a single replica generated 60%
// or more of the last check_rmcount, that replica is considered
// as a flooder replica

const int flood_protected_views = 500;
//the flooded replica is unmuted after
//flood_protected_views views

/* ----  END REPLICA FLOOD PROTECTION  ---- */

const int batch_size_limit = 1000;

/* Define SIGN_REQUESTS to force all requests to be signed by the client */
#define SIGN_REQUESTS 
//#define SIMULATE_SIGS

// clients do not sign the requests
#define NO_CLIENT_SIGNATURES


/* client timeout for retransmission of requests, in ms */
const int client_retrans_timeout = 150;

/* TCP port used by the PIRs when bootstrapping */
#define PIR_BOOTSTRAP_PORT 6000

/* TCP port used by the verifier when bootstrapping */
#define VERIFIER_BOOTSTRAP_PORT 6010

/*
 * TCP port on which the master protocol instance binds itself for
 * communications with the Verifier.
 * The port on which the other instances bind is PIR_VERIFIER_PORT + no_instance,
 * e.g. instance i -> PIR_VERIFIER_PORT + i
 */
#define PIR_VERIFIER_PORT 6020


// size of the circular buffer in shared memory
const int shared_mem_circular_buffer_size = 1000;

// ftok pathnames for the verifier <-> pirs circular buffer in shared memory
#define FTOK_VERIFIER_TO_PIR "/tmp/ftok_verifier_pir"
#define FTOK_PIR_TO_VERIFIER "/tmp/ftok_pir_verifier"


// for kzimp
#define KZIMP_CHAR_DEV_FILE "/dev/kzimp"


/****************************** BLACKLISTING ******************************/
//#undef NO_BLACKLISTING
#define NO_BLACKLISTING
#define PERIODIC_DISPLAY_PERC_VALID 1000000
#define NODE_BLACKLISTER_SLIDING_WINDOW_SIZE 20
#define READING_FROM_BLACKLISTED_NODES_PERIOD 100


// define it in order to deactivate the view changes
//#define VIEW_CHANGE_DEACTIVATED
#undef VIEW_CHANGE_DEACTIVATED

// instead of computing the throughput since the last checkpoint,
// compute it since the last view change
#define THROUGHPUT_ADAPTIVE_LAST_VIEW
//#undef THROUGHPUT_ADAPTIVE_LAST_VIEW

// If defined, then replace the test n_le >= n-f by n_le >= n-f-1 in NV_info::ch
// eck_comp()
// solves a bug with the replica flooding attack (only?)
#undef N_LE_GT_N_F_1

// send mask: if the bit at position i is set to 1, then messages will be sent to node i
// If 1 fault:
//   0xF: default value
//   0x7: node 3 floods
// If 2 faults:
//   0x7F: default value
//   0x1F: nodes 5 and 6 flood
#define send_mask (0x7F)

// attack 1: the primary of the master instance is correct.
// the attacker wants a protocol instance change to be voted
#undef ATTACK1

// attack 2: the primary of the master instance is malicious
// the attack does not want a protocol instance change to be voted
#undef ATTACK2

// define it if the flooder replica takes part in the protocol
#define FLOODER_TAKES_PART_IN_PROTOCOL
#undef FLOODER_TAKES_PART_IN_PROTOCOL

// define it if the forwarding thread does not flood and the replica floods
#define FLOODING_BUT_NOT_FOR_FORWARDER
#undef FLOODING_BUT_NOT_FOR_FORWARDER

// who is the faulty protocol instance?
#define FAULTY_PROTOCOL_INSTANCE (0)

// if defined, limit the size of the req list.
// used when there is an attack, by the protocol instance whose primary is faulty
#undef LIMIT_REQ_LIST_USAGE

// do we use TCP (or UDP) connections?
#define USE_TCP_CONNECTIONS (1)

// period of the pipe thr timer in ms
#define DEBUG_PERIODIC_THR_PERIOD 1000
#define DEBUG_PERIODICALLY_PRINT_THR
#undef DEBUG_PERIODICALLY_PRINT_THR

#endif // _parameters_h
