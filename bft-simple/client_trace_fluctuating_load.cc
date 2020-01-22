/*
 * client_trace_fluctuating_load.cc
 * Replay a real trace
 */

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <netinet/tcp.h> // for TCP_NODELAY
#include <math.h>

#include <stdint.h>
#include <unistd.h>
#include <vector>
#include <iostream>

#include "libbyz.h"
#include "th_assert.h"
#include "Timer.h"
#include "Client.h"
#include "Statistics.h"
#include "simple_benchmark.h"
#include "tcp_net.h"
#include "lat_req.h"


using namespace std;

vector<unsigned long> times_of_submission;

void read_trace(char *tracepath) {
   // read the file
   FILE *F = fopen(tracepath, "r");
   if (F == NULL) {
       fprintf(stderr, "Error while trying to open %s. Is the file existing?\n", tracepath);
       exit(-1);
   }

   unsigned long v;
   while (fscanf(F, "%lu", &v) != EOF) {
     times_of_submission.push_back(v);
   }

   fclose(F);

   /*
      cout << "The contents are:";
      for (int i=0; i < vec->size(); i++)
      cout << " " << vec->at(i);
      cout << endl;
      */
}

// initialize the client
void init_client(char *config, char *config_priv, int port, char *manager_ip,
    int *manager_socket_fd)
{
  if (config[0] == 0)
  {
    // Try to open default file
    strcpy(config, "./config");
  }

  if (config_priv[0] == 0)
  {
    // Try to open default file
    strcpy(config_priv, "config_private/template");
  }

  // Initialize client
  Byz_init_client(config, config_priv, port);
  Byz_reset_stats();

  fprintf(stderr, "Client %i initialized\n", node->id());

  // =======================================================

    // open socket to communicate with the manager
    if ((*manager_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
      th_fail("Could not create socket to communicate with manager");
    }

    // TCP NO DELAY
    int flag = 1;
    int result = setsockopt(*manager_socket_fd, IPPROTO_TCP, TCP_NODELAY,
        (char*) &flag, sizeof(int));

    if (result == -1)
    {
      fprintf(stderr,
          "Unable to set TCP_NODELAY on socket communicating with manager, exiting.\n");
      exit(-1);
    }

    // fill-in manager address
    Address desta;
    bzero((char*) &desta, sizeof(desta));
    desta.sin_addr.s_addr = inet_addr(manager_ip);
    desta.sin_family = AF_INET;
    desta.sin_port = htons(MANAGER_PORT);

    // connect to the manager
    while (true)
    {
      if (connect(*manager_socket_fd, (struct sockaddr*) &desta, sizeof(desta))
          < 0)
      {
        fprintf(stderr, "Client %i to manager: ", node->id());
        fprintf(stderr, " ...cannot connect to manager, attempting again..\n");
        sleep(1);
      }
      else
      {
        fprintf(stderr, "Client %i to manager: connection successful!\n",
            node->id());
        break;
      }
    }

    char filename[256];
    sprintf(filename, "latency_client_%i.dat", node->id());
    initialize_lat_req(filename);
}

// print (node->id(), time at which I have received the first mr_burst message)
// so that we can compute the throughput on the client side w.r.t the beginning of the experiment.
// Output is the file start_time_client_%i.dat
void print_start_time(struct timeval t) {
    char filename[256];
    sprintf(filename, "start_time_client_%i.dat", node->id());

    FILE *F = fopen(filename, "w");
    if (F) {
        fprintf(F, "%d\t%qu\t%qu\n", node->id(), (unsigned long long) t.tv_sec,
                (unsigned long long) t.tv_usec);
        fclose(F);
    } else {
        fprintf(stderr, "Client %d\t%qu\t%qu\n", node->id(), (unsigned long long) t.tv_sec, (unsigned long long) t.tv_usec);
    }
}


// handles a mr_end message
void handle_end_message(void)
{
  fprintf(stderr, "Client is terminating...");
  Byz_print_stats();
}

// handles a mr_burst message
void handle_burst_message(ManReq in, bool read_only, int manager)
{
  ManReq out;
  Byz_req req;
  float latency_mean = 0;

  Byz_reset_client();

  // Allocate request
  Byz_alloc_request(&req, Simple_size);
  th_assert(Simple_size <= req.size, "Request too big");

  if (in.size < 8)
  {
    req.size = 8;
  }
  else
  {
    req.size = in.size;
  }

  // Store data into request
  for (int i = 0; i < req.size; i++)
  {
    req.contents[i] = 0;
  }

  int* toto = (int*) req.contents;
  toto[0] = in.size;
  toto[1] = 0;

#ifdef DEBUG
  fprintf(stderr, "in.size is %iB, req.size is %iB\n", in.size, req.size);
#endif

  stats.zero_stats();

  Byz_rep rep;

  long nb_requests_burst = 0;

  long long tts;
  Timer thr_current;
  float thr_elapsed = 0;

  int next_submission = 0;
  long long time_of_submission = times_of_submission[next_submission] * time_unit;
  long long start_time = currentTime();
  //fprintf(stderr, "Next to submit is at %qd\n", time_of_submission);

  thr_current.start();
  while (true)
  {
    if (next_submission < (int)times_of_submission.size())
    {
      long long elapsed_time = diffTime(currentTime(), start_time);
      if (elapsed_time >= time_of_submission)
      {
        //fprintf(stderr, "Submitting @%qd (should be %qd)\n", elapsed_time, time_of_submission);
        struct timeval snd_req = currentTime_gettimeofday();
        long long view_number = Byz_invoke(&req, &rep, read_only, false);
        struct timeval rcv_resp = currentTime_gettimeofday();

        // Free reply
        Byz_free_reply(&rep);

        // add latency
        // rcv_resp and snd_req are in usec; latency_mean in ms
        float latency = (float)(diffTime_timeval(rcv_resp, snd_req)) / 1000.0;
        latency_mean += latency;

#ifdef SAVE_LATENCIES
         if (in.start_logging == 1 && view_number != -1)
         {
           add_req_times(view_number, snd_req, rcv_resp);
         }
#endif

        nb_requests_burst++;

        next_submission++;
        time_of_submission = times_of_submission[next_submission] * time_unit;
        //fprintf(stderr, "Next to submit is at %qd\n", time_of_submission);
      }
      else {
         tts = time_of_submission - elapsed_time - 50;
         if (tts > 0)
         {
             //fprintf(stderr, "(%i-1) sleep of %qd usec\n", node->id(), tts);
             usleep(tts);
         }
         continue;
       }


      //elapsed_time = diffTime(currentTime(), start_time);
      //tts = time_of_submission - elapsed_time - 200;
      if (next_submission >= 1)
         tts = time_of_submission - (times_of_submission[next_submission-1] * time_unit) - 50;
      else
         tts = time_of_submission - 50;
      if (tts > 0)
      {
          //fprintf(stderr, "(%i-1) sleep of %qd usec\n", node->id(), tts);
          usleep(tts);
      }
    } else {
        // send a message to the manager
      thr_elapsed = thr_current.elapsed();

      float current_thr = nb_requests_burst / thr_elapsed;

      // send stats message to manager
      // latency = 0 if you do not want the stats to be taken into account by the manager
      out.type = mr_response;
      out.client_id = node->id();
      out.latency_mean = latency_mean / nb_requests_burst;
      out.nb_requests = nb_requests_burst;
      out.limit_thr = current_thr;

#ifdef DEBUG
      fprintf(stderr, "Client %i mean_lat= %f\tmean_thr= %f\n", out.client_id,
          out.latency_mean, out.limit_thr);
#endif

      //Client sends a periodic message to the manager
      sendMsg(manager, (void*) (&out), sizeof(out));

      // maybe this will be the last burst needed for the manager
      finalize_lat_req(0);
      latency_mean = 0;
      nb_requests_burst = 0;
      thr_current.stop();
      thr_current.reset();
      thr_current.start();
      thr_elapsed = 0;

      break;
    }
 }

  Byz_free_request(&req);
}

/**
 * Main method of client.
 */
int CLIENT_MAIN(int argc, char **argv)
{
  char config[PATH_MAX];
  char config_priv[PATH_MAX];
  char tracedir[PATH_MAX];
  char tracefile[PATH_MAX];
  char manager_ip[16];
  bool read_only = false;
  int user_id = -1;
  config[0] = config_priv[0] = manager_ip[0] = tracedir[0] = 0;
  short port = 0;
  int manager_socket_fd;
  ManReq in;
  struct timeval experiment_start_time;

  // Process command line options.
  int opt;
  while ((opt = getopt(argc, argv, "c:C:p:m:u:t:")) != EOF)
  {
    switch (opt)
    {
    case 'p':
      port = atoi(optarg);
      break;

    case 'c':
      strncpy(config, optarg, PATH_MAX);
      config[PATH_MAX] = 0;
      break;

    case 'C':
      strncpy(config_priv, optarg, PATH_MAX);
      config[PATH_MAX] = 0;
      break;

    case 'm':
      strncpy(manager_ip, optarg, 16);
      manager_ip[15] = 0;
      break;

    case 'u':
      user_id = atoi(optarg);
      break;

    case 't':
      strncpy(tracedir, optarg, PATH_MAX);
      tracedir[PATH_MAX] = 0;
      break;

    default:
      fprintf(
          stderr,
          "%s -c config_file -C config_priv_file -p port -m manager_ip -u user_id -t tracedir",
          argv[0]);
      exit(-1);
    }
  }

  snprintf(tracefile, 256, "%s/client_%d.dat", tracedir, user_id);

  // Init the client.
  init_client(config, config_priv, port, manager_ip, &manager_socket_fd);
  read_trace(tracefile);
  init_clock_mhz();

  fprintf(stderr, "I am client %d and I will use trace %s. time_unit= %f\n", node->id(), tracefile, time_unit);

  // get a request from the manager
  int ret = 0;
  ret = recvMsg(manager_socket_fd, (void*) &in, sizeof(in));

#ifdef DEBUG
  fprintf(stderr, "New request received:\n\ttype = %i\n\tsize = %i\n\tnb requests = %i\n",
      in.type, in.size, in.nb_requests);
#endif

  if (in.type == mr_burst)
  {
    // save time at which I have received the first mr_burst message
    experiment_start_time = currentTime_gettimeofday();

    // handle_burst_message takes the proportion of UNVALID requests
    handle_burst_message(in, read_only, manager_socket_fd);
  }

  finalize_lat_req(1);

  print_start_time(experiment_start_time);

  sleep(3600);

  return 0;
}
