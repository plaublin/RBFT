/*
 * defs.h
 *
 * Contains the things to include
 * and typedefs
 */

#ifndef SIMPLE_BENCHMARK_
#define SIMPLE_BENCHMARK_

#define MANAGER_PORT 3456
#define CLIENTS_PORT 10000

static const int Simple_size = 4096;

typedef struct sockaddr_in Address;

// types of requests
static const int mr_ready = 0;
static const int mr_burst = 1;
static const int mr_response = 2;
static const int mr_end = 3;

// clients send periodically a mr_response message to the manager
// so that it can aggregates values from the clients and computes stats.
// This value is in seconds.
static const float periodic_stats_sending = 10.0;

// speed of the replay for real traces
// time_unit = 1 -> submissions in usec
// time_unit = 1000 -> submissions in ms
// time_unit = 1000000 -> submissions in sec
const float time_unit = 10;

// the manager request
typedef struct ManReq
{
  int type; // see static const int above
  int client_id;
  long nb_requests; // number of requests in the burst
  int size; // size, in bytes, of the request and the reply
  float latency_mean; // the mean latency in ms, used only when type = mr_response
  float limit_thr; // limit throughput
  int start_logging; // if 1, then the clients start logging useful information
} ManReq;


// define SAVE_LATENCIES if you want the clients to save the latency of every request they send
// works only for client.cc (latencies of client_trace_fluctuating_load.cc are always saved)
#undef SAVE_LATENCIES


// If set, the manager exits after 1 burst and measures the throughput as the time
// elapsed between the send of the start_burst message and the reception of the last
// end_burst message
#undef MANAGER_FOR_FLUCTUATING_LOAD_TRACE

// attack 1: the primary of the master instance is correct.
// the attacker wants a protocol instance change to be voted
#undef ATTACK1

#endif /* SIMPLE_BENCHMARK_ */
