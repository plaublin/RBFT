#include <stdio.h>

#include "Verifier_stats.h"
#include "parameters.h"
#include "Time.h"
#include "Verifier.h"
#include "Node.h"

extern long long clock_mhz;
extern Verifier *replica;

Verifier_stats::Verifier_stats(int num_replicas2, int num_clients2, int num_pirs2)
{
  num_replicas = num_replicas2;
  num_clients = num_clients2;
  nb_pirs = num_pirs2;

  nb_requests_for_thr = new long[nb_pirs];

  request_send_to_pirs_time
      = new std::unordered_map<Request_id, struct send2pirtime>[num_clients];
  nb_reqs_for_latency = new long*[num_clients];
  sum_latency = new long long*[num_clients];
  average_latency = new double*[num_clients];

  for (int i = 0; i < num_clients; i++)
  {
    nb_reqs_for_latency[i] = new long[nb_pirs];
    sum_latency[i] = new long long[nb_pirs];
    average_latency[i] = new double[nb_pirs];
  }

  reset_throughput();
  reset_latency();
}

Verifier_stats::~Verifier_stats()
{
  while (!monitored_throughput.empty())
  {
    delete[] monitored_throughput.back();
    monitored_throughput.pop_back();
  }

  delete[] nb_requests_for_thr;

  for (int i = 0; i < num_clients; i++)
  {
    delete[] nb_reqs_for_latency[i];
    delete[] sum_latency[i];
    delete[] average_latency[i];

    for (std::unordered_map<Request_id, struct send2pirtime>::iterator it =
        request_send_to_pirs_time[i].begin(); it
        != request_send_to_pirs_time[i].end();)
    {
      request_send_to_pirs_time[i].erase(it++);
    }
  }
  delete[] nb_reqs_for_latency;
  delete[] sum_latency;
  delete[] average_latency;
  delete[] request_send_to_pirs_time;
}

void Verifier_stats::measure_mac_sig_costs(void)
{
  Time cost_of_verifying_sig_in_cycles;
  Time cost_of_generating_MAC_in_cycles;
  Time cost_of_verifying_MAC_in_cycles;

  Request *req = new Request((Request_id) 0);

  cost_of_verifying_sig_in_cycles = req->get_cost_of_verifying_sig();
  cost_of_generating_MAC_in_cycles = req->get_cost_of_generating_mac();
  cost_of_verifying_MAC_in_cycles = req->get_cost_of_verifying_mac();

  fprintf(
      stderr,
      "cost_of_verifying_sig_in_cycles = %qd, cost_of_generating_MAC_in_cycles = %qd, cost_of_verifying_MAC_in_cycles = %qd\n",
      cost_of_verifying_sig_in_cycles, cost_of_generating_MAC_in_cycles,
      cost_of_verifying_MAC_in_cycles);

  delete req;
}

void Verifier_stats::add_reply_for_latency(Time t, int cid, Request_id rid, int pir)
{
  int idx = cid-num_replicas;
  double lat = diffTime(t, request_send_to_pirs_time[idx][rid].t) / 1000.0;
  request_send_to_pirs_time[idx][rid].bitmap |= (1 << pir);
  if (request_send_to_pirs_time[idx][rid].bitmap == (nb_pirs == 1 ? 3 : 7)) {
      request_send_to_pirs_time[idx].erase(rid);
  }

  nb_reqs_for_latency[idx][pir]++;
  sum_latency[idx][pir] += lat;
  average_latency[idx][pir] = sum_latency[idx][pir] / (float)nb_reqs_for_latency[idx][pir];

  if (pir == 0 && (idx == 0 || idx == 50)) {
    fprintf(stderr, "LATENCY: req (%d, %qd) from PIR %d: %.2f\n", cid, rid, pir, lat);
    /*
    if (pir == 0)
      latencies_of_client0_pir0.push_back(lat);
    else if (pir == 1)
      latencies_of_client0_pir1.push_back(lat);
    */
  }

  if (lat > MAX_ACCEPTABLE_LATENCY) {
    fprintf(stderr, "LATENCY: too high: %.2f > %.2f\n", lat, MAX_ACCEPTABLE_LATENCY);
    lat_too_high = true;
  }
}

void Verifier_stats::compute_performance_of_pirs(void)
{
  int total_nb_req_for_thr = 0;
  double *last_monitored_throughput = new double[nb_pirs];

  long long current_time = currentTime();

  for (int i = 0; i < nb_pirs; i++)
  {
    //throughput
    last_monitored_throughput[i] = nb_requests_for_thr[i] / (double) diffTime(
        current_time, start_time) * 1000000.0;
    total_nb_req_for_thr += nb_requests_for_thr[i];

    //fprintf(stderr, "PIR%d, thr=%f, lat=%f, nb_req_thr=%d, nb_req_lat=%d, sum_lat=%f\n", i, last_monitored_throughput[i], last_monitored_latency[i], nb_requests_for_thr[i], nb_requests_for_lat[i], sum_lat[i]);
  }

  if (total_nb_req_for_thr > 0)
  {
    monitored_throughput.push_back(last_monitored_throughput);
  }
}

// return true if the latency is not acceptable:
//  -either particular request has a too high latency
//  -or the average for one client is greater on the master instance
bool Verifier_stats::latency_not_acceptable()
{
  if (lat_too_high)
  {
    return true;
  }

  // we need to compare the average latency of every protocol instance
  for (int c = 0; c < num_clients; c++)
  {
    double master = average_latency[c][0];
    double backup = average_latency[c][1];
    for (int i = 2; i < nb_pirs; i++)
    {
      backup += average_latency[c][i];
    }
    backup /= (nb_pirs-1);

    if (master > 0.0 && backup > 0.0) {
      double ratio = (master - backup) / master;

      if (ratio > DELTA_LATENCY) {
        fprintf(stderr, "LATENCY: client %d, avg= ( %.2f %.2f ), ratio=%.2f\n", c+num_replicas, average_latency[c][0], average_latency[c][1], ratio);
        return true;
      }
    }
  }

  return false;
}

double Verifier_stats::get_throughput_ratio(double *thr, double *master,
    double *backup)
{
  // get perf of master
  *master = thr[0];

  // get the values for the f+1 best instances that are not the master
  *backup = thr[1];
  for (int i=2; i<nb_pirs; i++) {
      *backup = MAX(*backup, thr[i]);
  }

  // compute the ratio
  return (*master - *backup) / *master;
}

void Verifier_stats::print_stats(void)
{
  double master, backup;
  double *thr;
  double tr, min_thr_ratio, max_thr_ratio;

  //int nb_snapshots = MIN(monitored_latency.size(), monitored_throughput.size());
  int nb_snapshots = monitored_throughput.size();

  fprintf(stderr, "There are %d snapshots\n", nb_snapshots);

  // init the min and max ratio
  if (nb_snapshots > 0)
  {
    thr = monitored_throughput[0];

    tr = get_throughput_ratio(thr, &master, &backup);

    min_thr_ratio = max_thr_ratio = tr;
  }

  fprintf(stderr, "#snapshot_id");
  for (int i=0; i<nb_pirs; i++) {
      fprintf(stderr, "\tthr_PIR%d", i);
  }
  fprintf(stderr, "\tthr_ratio\n");
  
  for (int snapshot_id = 0; snapshot_id < nb_snapshots; snapshot_id++)
  {
    fprintf(stderr, "%d", snapshot_id);

    thr = monitored_throughput[snapshot_id];

    tr = get_throughput_ratio(thr, &master, &backup);
    min_thr_ratio = MIN(tr, min_thr_ratio);
    max_thr_ratio = MAX(tr, max_thr_ratio);

    for (int i = 0; i < nb_pirs; i++)
    {
      fprintf(stderr, "\t%f", thr[i]);
    }
    fprintf(stderr, "\t%f\n", tr);
  }

  fprintf(stderr, "#thr_ratio, min=%f, max=%f\n", min_thr_ratio, max_thr_ratio);
}

// printing all latencies for off-line analysis
void Verifier_stats::print_latencies_for_offline_analysis(void) {
  fprintf(stderr, "Latencies of client 0, pir0:\nrid\tlat(ms)\n");
  for (int i=0; i<latencies_of_client0_pir0.size(); i++) {
      fprintf(stderr, "%d\t%.2f\n", i, latencies_of_client0_pir0[i]);
  }
  fprintf(stderr, "=====================\n");

  fprintf(stderr, "Latencies of client 0, pir1:\nrid\tlat(ms)\n");
  for (int i=0; i<latencies_of_client0_pir1.size(); i++) {
      fprintf(stderr, "%d\t%.2f\n", i, latencies_of_client0_pir1[i]);
  }
  fprintf(stderr, "=====================\n");
}
