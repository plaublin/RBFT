/*
 * client.cc
 * Get requests from the manager, send them to the replicas,
 * OPEN LOOP: one thread sends requests, another one receives replies
 */

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <netinet/tcp.h> // for TCP_NODELAY
#include <math.h>
#include <pthread.h>
#include "libbyz.h"
#include "th_assert.h"
#include "Timer.h"
#include "Client.h"
#include "Statistics.h"
#include "parameters.h"
#include "simple_benchmark.h"
#include "tcp_net.h"
#include "lat_req.h"

#define NB_MAX_TIMERS (max_out)

// request id of the last sent request
extern unsigned long long last_request_id;

int manager_socket_fd;

// initialize the client
void init_client(char *config, char *config_priv, int port, char *manager_ip,
    int *manager_socket_fd, bool flooder)
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

  // print config
  fprintf(
      stderr,
      "Launching client...\n\tconfig: %s\n\tconfig_private: %s\n\tport: %i\n\tmanager ip: %s\n",
      config, config_priv, port, manager_ip);

  // Initialize client
  Byz_init_client(config, config_priv, port);
  Byz_reset_stats();

  fprintf(stderr, "Client %i initialized\n", node->id());

  // =======================================================

  if (!flooder)
  {
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
  }
}

// handles a mr_end message
void handle_end_message(void)
{
  fprintf(stderr, "Client is terminating...");
  Byz_print_stats();
}

// handles a mr_burst message
void handle_burst_message(ManReq in, float proportion_unfaithful_requests,
    bool read_only, int manager)
{
  Byz_req req;

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

  float thr_elapsed = 0;

  Timer thr_t;
  Timer thr_current;

  thr_t.start();
  thr_current.start();

  long nb_requests_burst = 0;

  long nb_iter = 0;
  Time start;
  start = currentTime();

  while (true)
  {
    // if not in advance then execute a request
    thr_elapsed = thr_t.elapsed();
    if (nb_iter / thr_elapsed <= in.limit_thr)
    {
#ifndef ATTACK1
      if (proportion_unfaithful_requests > 0.0)
      {
        Byz_open_loop_send_request(&req, read_only, true);
      }
      else
      {
#else
        bool faultyClient = false;
        if (proportion_unfaithful_requests > 0.0)
        {
          faultyClient = true;
        }
#endif

#ifndef ATTACK1
        Byz_open_loop_send_request(&req, read_only, false);
#else
        Byz_open_loop_send_request(&req, read_only, faultyClient);
#endif

#ifndef ATTACK1
      }
#endif

      nb_iter++;
      nb_requests_burst++;
    }
    else// else, sleep for a little while
    {
      long sleeping_time = nb_iter / in.limit_thr * 1000000 - thr_elapsed
          * 1000000 - 10000;
      // 10000 is the epsilon, to wake up a little earlier
      // sleeping_time is in microseconds (10^{-6})
      if (sleeping_time < 0)
        sleeping_time = 0;

      usleep(sleeping_time);
    }
  }

  thr_t.stop();
  Byz_free_request(&req);
}

// recv replies from the system
void *do_receiver(void *arg)
{
  ManReq out;

  Byz_rep rep;

  float thr_elapsed = 0;

  Timer thr_t;
  Timer thr_current;

  thr_t.start();
  thr_current.start();

  long nb_iter = 0;
  long nb_requests_burst = 0;

  Time start;
  start = currentTime();

  while (true)
  {
    // Receive reply
    unsigned long long rid;
    Byz_open_loop_recv_reply(&rid, &rep);

    // Free reply
    Byz_free_reply(&rep);

    nb_iter++;
    nb_requests_burst++;

    // Periodically send a message to the manager (every X seconds)
    thr_elapsed = thr_current.elapsed();
    if (thr_elapsed >= periodic_stats_sending)
    {
#ifdef DEBUG
      fprintf(stderr, "Client %i is sending a periodic message to the manager after %f sec\n", out.client_id, thr_elapsed);
#endif

      thr_current.stop();

      float current_thr = nb_requests_burst / thr_elapsed;

      // send stats message to manager
      out.type = mr_response;
      out.client_id = node->id();
      out.nb_requests = nb_requests_burst;
      out.limit_thr = current_thr;

      // openloop: not interested in the latency
      // do not set it to 0 if the client is correct, otherwise the manager will exit
      if (nb_requests_burst > 0) {
        out.latency_mean = 1;
      } else {
        out.latency_mean = 0;
      }

#ifdef DEBUG
      fprintf(stderr, "Client %i mean_lat= %f\tmean_thr= %f\n", out.client_id,
          out.latency_mean, out.limit_thr);
#endif

      //Client sends a periodic message to the manager
      sendMsg(manager_socket_fd, (void*) (&out), sizeof(out));

      // maybe this will be the last burst needed for the manager
      nb_requests_burst = 0;
      thr_current.reset();
      thr_current.start();
    }
  }

  thr_t.stop();
  return NULL;
}

/**
 * Main method of client.
 */
int CLIENT_MAIN(int argc, char **argv)
{
  char config[PATH_MAX];
  char config_priv[PATH_MAX];
  char manager_ip[16];
  bool read_only = false;
  double proportion_g = 1.0;
  bool flooder = false;
  config[0] = config_priv[0] = manager_ip[0] = 0;
  short port = 0;
  ManReq in;

  // Process command line options.
  int opt;
  while ((opt = getopt(argc, argv, "c:C:p:m:g:f")) != EOF)
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

    case 'g':
      proportion_g = atof(optarg);
      break;

    case 'f':
      flooder = true;
      break;

    default:
      fprintf(
          stderr,
          "%s -c config_file -C config_priv_file -p port -m manager_ip -g proportion_g_of_faithful_requests -f",
          argv[0]);
      exit(-1);
    }
  }

  fprintf(stderr,
      "I am a client that will generate a proportion %f of bad requests\n",
      proportion_g);

  // Init the client.
  init_client(config, config_priv, port, manager_ip, &manager_socket_fd,
      flooder);

  if (flooder)
  {
    fprintf(stderr, "I am a client that will flood the system\n");
    Byz_run_flooding_client();
    return 0;
  }

  //create the rcv thread
  pthread_t rcv_thread;
  int rc = pthread_create(&rcv_thread, NULL, do_receiver, NULL);
  if (rc)
  {
    fprintf(stderr, "Failed to create the receiver thread\n");
    return 0;
  }

  // get a request from the manager
  int ret = 0;
  ret = recvMsg(manager_socket_fd, (void*) &in, sizeof(in));

#ifdef DEBUG
  fprintf(stderr, "New request received:\n\ttype = %i\n\tsize = %i\n\tnb requests = %i\n",
      in.type, in.size, in.nb_requests);
#endif

  if (in.type == mr_burst)
  {
    // handle_burst_message takes the proportion of UNVALID requests
    handle_burst_message(in, proportion_g, read_only, manager_socket_fd);
  }

  fprintf(stderr, "Client finished, exiting!\n");

  sleep(60);

  return 0;
}
