#ifndef VERIFIER_STATS_
#define VERIFIER_STATS_

#include <stdio.h>
#include <vector>
#include <unordered_map>

#include "Cycle_counter.h"
#include "Time.h"
#include "parameters.h"
#include "types.h"


struct send2pirtime {
    char bitmap; // bitmap[i] = 1 <=> the request has been ordered by PIR i 
    Time t;
};


// This class manages statistics from the PIRs at the Verifier.
class Verifier_stats
{
public:
  Verifier_stats(int num_replicas2, int num_clients2, int num_pirs2);
  ~Verifier_stats();

  void measure_mac_sig_costs(void);

  void reset_throughput(void);
  void reset_latency(void);

  void add_request_for_throughput(int pir, int n);
  void add_request_for_latency(int cid, Request_id rid);
  void add_reply_for_latency(Time t, int cid, Request_id rid, int pir);

  void compute_performance_of_pirs(void);

  // return true if the latency is not acceptable:
  //  -either particular request has a too high latency
  //  -or the average for one client is greater on the master instance
  bool latency_not_acceptable();

  // return true if the throughput of the master instance, compared to the throughput of the
  // best f backup instances, is not acceptable; false otherwise.
  // If true is returned, then master and backup are set to the throughput of (respectively
  // the master instance and the best f backup instances.
  bool throughput_not_acceptable(double *master, double *backup);

  void print_stats(void);

  void print_latencies_for_offline_analysis(void);

  // an array of size the number of pirs. To be used when printing the stats before dying
  unsigned long *nb_not_logged_requests;
  
private:
  double get_latency_ratio(double *lat, double *master, double *backup);
  double get_throughput_ratio(double *thr, double *master, double *backup);

  int num_clients;
  int num_replicas;
  int nb_pirs;

  long long start_time;
  long *nb_requests_for_thr;
  std::vector<double*> monitored_throughput;

  std::unordered_map<Request_id, struct send2pirtime> *request_send_to_pirs_time; // request_send_to_pirs_time[cid][rid] = latency for request (cid, rid)
  long **nb_reqs_for_latency;
  long long **sum_latency;
  double **average_latency;
  bool lat_too_high;
  std::vector<double> latencies_of_client0_pir0;
  std::vector<double> latencies_of_client0_pir1;
};


inline void Verifier_stats::reset_throughput(void)
{
  //fprintf(stderr, "reset throughput\n");
  for (int i = 0; i < nb_pirs; i++)
  {
    nb_requests_for_thr[i] = 0;
  }
  start_time = currentTime();
}


inline void Verifier_stats::reset_latency(void)
{
  for (int i = 0; i < num_clients; i++)
  {
    for (int j = 0; j < nb_pirs; j++)
    {
      nb_reqs_for_latency[i][j] = 0;
      sum_latency[i][j] = 0;
      average_latency[i][j] = 0;
    }
  }

  lat_too_high = false;
}

inline void Verifier_stats::add_request_for_throughput(int pir, int n)
{
  nb_requests_for_thr[pir] += n;
}

inline void Verifier_stats::add_request_for_latency(int cid, Request_id rid)
{
  int idx = cid - num_replicas;
  struct send2pirtime s;
  s.bitmap = 0;
  s.t = currentTime();
  request_send_to_pirs_time[idx][rid] = s;
}

// return true if the throughput of the master instance, compared to the throughput of the
// best f backup instances, is not acceptable; false otherwise.
// If true is returned, then master and backup are set to the throughput of (respectively
// the master instance and the best f backup instances.
inline bool Verifier_stats::throughput_not_acceptable(double *master, double *backup)
{
  if (monitored_throughput.size() > 0) {
    double r = get_throughput_ratio(monitored_throughput.back(), master, backup);
    return (r < DELTA_THROUGHPUT);
  } else {
    return false;
  }
}

#endif
