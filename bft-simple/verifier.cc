/*
 * replica.cc
 * this is the replica :)
 */

#include <stdio.h>
#include <strings.h>
#include <signal.h>

#include "Statistics.h"
#include "libbyz.h"
#include "attacks.h"
#include "simple_benchmark.h"

/*
static void dump_profile(int sig)
{
  profil(0, 0, 0, 0);

  stats.print_stats();

  exit(0);
}
*/

/*
 * function executed by the replica when it receives a request
 * inb: the incoming request
 * outb: the reply
 * non_det: non_deterministic request?
 * client: client id
 * ro: true if the request is read-only, false otherwise
 * exec_command_delay: time, in ns, during which the request execution is delayed
 */
int exec_command(Byz_req *inb, Byz_rep *outb, Byz_buffer *non_det, int client,
    bool ro, long int exec_command_delay)
{
  int size = ((int*) inb->contents)[0];

#ifdef DEBUG
  //fprintf(stderr, "Received a request of size %i from client %i\n", size, client);
#endif


#ifdef COSTLY_EXECUTION
  for(long i=0;i<10000;i++);
#endif

  if (size > 0)
  {
    bzero(outb->contents, size);
  }

  if (size > 8)
  {
    outb->size = size;
  }
  else
  {
    outb->size = 8;
  }

#ifdef DEBUG
  //fprintf(stderr, "Reply size is %i.\n", outb->size);
#endif

  return 0;
}

int VERIFIER_MAIN(int argc, char **argv)
{
  // Process command line options.
  char config[PATH_MAX];
  char config_priv[PATH_MAX];
  config[0] = config_priv[0] = 0;
  int byz_pre_prepare_delay = 0;
  int exec_command_delay = 0;

  int opt;
  while ((opt = getopt(argc, argv, "c:p:z")) != EOF)
  {
    switch (opt)
    {
    case 'c':
      strncpy(config, optarg, PATH_MAX);
      break;

    case 'p':
      strncpy(config_priv, optarg, PATH_MAX);
      break;

    case 'z':
      // [attack] Flood network with random messages
      setAttack(FLOOD_MAX);
      break;

    default:
      fprintf(stderr, "%s -c config_file -p config_priv_file -z", argv[0]);
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
    strcpy(config_priv, "config_private/template");
  }

  // signal handler to dump profile information.
  // struct sigaction act;
  // act.sa_handler = dump_profile;
  // sigemptyset(&act.sa_mask);
  // act.sa_flags = 0;
  // sigaction(SIGINT, &act, NULL);
  // sigaction(SIGTERM, &act, NULL);

  int mem_size = 2054 * 8192;
  char *mem = (char*) valloc(mem_size);
  bzero(mem, mem_size);

  setAttack(attack_mode);

  fprintf(stderr, "Going to initialize the verifier with files %s and %s\n", config, config_priv);

  // 1) Init the Verifier.
  int init_return = Byz_init_replica(
      config,
      config_priv,
      mem,
      mem_size,
      exec_command,
      0,
      0,
      byz_pre_prepare_delay,
      exec_command_delay);

  if (init_return < 0) {
    fprintf(stderr, "Unable to init verifier. Exiting here.");
    exit(-1);
  }

  fprintf(stderr, "Verifier has been initialized!\n");

  // 2) Start loop executing requests.
  Byz_replica_run();

  return 0;
}
