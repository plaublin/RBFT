/*
 * client.cc
 * Get requests from the manager, send them to the replicas, wait for the reply  and send it back to the manager
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "libbyz.h"
#include "th_assert.h"
#include "Timer.h"
#include "Client.h"
#include "Statistics.h"
#include "simple_benchmark.h"

int CLIENT_SOLO_MAIN(int argc, char **argv)
{
  char config[PATH_MAX];
  char config_priv[PATH_MAX];
  bool read_only = false;
  bool faultyClient = false;
  config[0] = config_priv[0] = 0;
  short port = 0;

  // Process command line options.
  int opt;
  while ((opt = getopt(argc, argv, "c:C:p:")) != EOF)
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

    default:
      fprintf(stderr, "%s -c config_file -C config_priv_file -p port", argv[0]);
      exit(-1);
    }
  }

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

#ifdef DEBUG
  // print config
  fprintf(stderr, "Launching client...\n\tconfig: %s\n\tconfig_private: %s\n\tport: %i\n"
      , config, config_priv, port);
#endif

  // Initialize client
  Byz_init_client(config, config_priv, port);
  Byz_reset_stats();

#ifdef DEBUG
  fprintf(stderr, "Client %i initialized\n", node->id());
#endif

  Byz_req req;
  Timer t;
  float latency_mean = 0;

  Byz_reset_client();

  // Allocate request
  Byz_alloc_request(&req, Simple_size);
  th_assert(Simple_size <= req.size, "Request too big");

  // in.size is an int, but contents is a char*
  // we convert in.size in kB and then in char
  int insize = 0;
  char sizeAsAChar = (char) (insize / 1000);

  if (insize < 8)
  {
    req.size = 8;
  }
  else
  {
    req.size = insize;
  }

  // Store data into request
  for (int i = 0; i < req.size; i++)
  {
    req.contents[i] = sizeAsAChar;
  }

#ifdef DEBUG
  fprintf(stderr, "insize is %iB, req.size is %iB\n", insize, req.size);
#endif

  stats.zero_stats();

  Byz_rep rep;
  Timer thr_t;

  thr_t.start();

  long nb_iter = 0;
  while (1)
  {
    t.start();
    Byz_invoke(&req, &rep, read_only, faultyClient);
    t.stop();

    // Free reply & request
    Byz_free_reply(&rep);

    // add latency
    // t.elapsed() is in seconds. latency_mean in ms
    latency_mean += (t.elapsed()) * 1000;
    t.reset();

    // sleep during 2 seconds
    usleep(2000000);

    nb_iter++;

    if (nb_iter % 10 == 0)
    {
      fprintf(stderr, "iter= %li\tthr= %li\tlat= %e\n", nb_iter, nb_iter
          / thr_t.elapsed(), latency_mean / nb_iter);
    }
  }

  t.stop();
  thr_t.stop();
  Byz_free_request(&req);

  fprintf(stderr, " Bye!\n");

  return 0;
}
