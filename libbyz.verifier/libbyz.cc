#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "libbyz.h"
#include "Array.h"
#include "Request.h"
#include "Reply.h"
#include "Client.h"
#include "Statistics.h"
#include "State_defs.h"
#if defined(PIR_ONLY)
#include "PIR.h"
#elif defined(VERIFIER_ONLY)
#include "Verifier.h"
#endif

// FLAGS for faulty server behavior
bool _corruptClientMAC = false;

bool corruptClientMAC()
{
  return _corruptClientMAC;
}

void setCorruptClientMAC(bool val)
{
  _corruptClientMAC = val;
}

#include "attacks.h"

static void wait_chld(int sig)
{
  // Get rid of zombies created by sfs code.
  while (waitpid(-1, 0, WNOHANG) > 0)
    ;
}

int Byz_init_client(char *conf, char *conf_priv, short port)
{
  // signal handler to get rid of zombies
  struct sigaction act;
  act.sa_handler = wait_chld;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  sigaction(SIGCHLD, &act, NULL);

  FILE *config_file = fopen(conf, "r");
  if (config_file == 0)
  {
    fprintf(stderr, "libbyz: Invalid configuration file %s \n", conf);
    return -1;
  }

  FILE *config_priv_file = fopen(conf_priv, "r");
  if (config_priv_file == 0)
  {
    fprintf(stderr, "libbyz: Invalid private configuration file %s \n", conf);
    return -1;
  }

  // Initialize random number generator
  srand48(getpid());

  Client* client = new Client(config_file, config_priv_file, port);
  node = client;
  return 0;
}

void Byz_reset_client()
{
  ((Client*) node)->reset();
}

int Byz_alloc_request(Byz_req *req, int size)
{
  Request* request = new Request((Request_id) 0);
  if (request == 0)
    return -1;

  int len;
  // store_command uses the pointer to len in order to modify it
  req->contents = request->store_command(len);
  req->size = len;
  req->opaque = (void*) request;
  return 0;
}

int Byz_send_request(Byz_req *req, bool ro, bool faultyClient)
{
  Request *request = (Request *) req->opaque;
#ifdef FAULTY_FOR_MASTER
  if(faultyClient){
    ((Client*) node)->set_rid(node->new_rid());
  }
#endif

  request->request_id() = ((Client*) node)->get_rid();

#ifndef SIGN_REQUESTS
  request->authenticate(req->size, ro, faultyClient);
#else
  request->sign(req->size);
#endif

  bool retval = ((Client*) node)->send_request(request, faultyClient);
  return (retval) ? 0 : -1;
}

long long Byz_recv_reply(Byz_rep *rep)
{
  Reply *reply = ((Client*) node)->recv_reply();
  if (reply == NULL)
    return -1;
  rep->contents = reply->reply(rep->size);
  rep->opaque = reply;

  // return the current view number
  return reply->view();
}

int Byz_invoke(Byz_req *req, Byz_rep *rep, bool ro, bool faultyClient)
{
  if (Byz_send_request(req, ro, faultyClient) == -1)
    return -1;
  return Byz_recv_reply(rep);
}

/* run the flooding client, that sends invalid messages of 9kB to all the replicas */
int Byz_run_flooding_client(void) {
  ((Client*)node)->flood_nodes();
  return 0;
}


void Byz_free_request(Byz_req *req)
{
  Request *request = (Request *) req->opaque;
  delete request;
}

void Byz_free_reply(Byz_rep *rep)
{
  Reply *reply = (Reply *) rep->opaque;
  delete reply;
}

// recv reply. If there is no reply, wait for at most timeout usec.
// Used to empty the TCP buffer.
// return the number of received messages
int Byz_recv_reply_noblock(long timeout)
{
  return ((Client*)node)->recv_reply_noblock(timeout);
}

/* OPEN LOOP */

// send the request req
int Byz_open_loop_send_request(Byz_req *req, bool ro, bool faultyClient) {
  Request *request = (Request *) req->opaque;
  request->request_id() = ((Client*) node)->get_rid();

#ifndef SIGN_REQUESTS
  request->authenticate(req->size, ro, faultyClient);
#else
  request->sign(req->size);
#endif

  bool retval = ((Client*) node)->send_request_open_loop(request, faultyClient);
  return (retval) ? 0 : -1;
}

// Recv a reply. Return the view number. Set *rid to the id of the received reply.
int Byz_open_loop_recv_reply(unsigned long long *rid, Byz_rep *rep) {
  Reply *reply = ((Client*) node)->recv_reply_open_loop();
  if (reply == NULL)
    return -1;
  rep->contents = reply->reply(rep->size);
  rep->opaque = reply;

  *rid = reply->request_id();
  return reply->view();
}

int Byz_init_replica(
    char *conf,
    char *conf_priv,
    char *mem,
    unsigned int size,
    int(*exec)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool, long int),
    void(*comp_ndet)(Seqno, Byz_buffer *),
    int ndet_max_len,
    long int byz_pre_prepare_delay,
    long int exec_command_delay)
{
  // signal handler to get rid of zombies
  struct sigaction act;
  act.sa_handler = wait_chld;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  sigaction(SIGCHLD, &act, NULL);

  FILE *config_file = fopen(conf, "r");
  if (config_file == 0)
  {
    fprintf(stderr, "libbyz: Invalid configuration file %s \n", conf);
    return -1;
  }

  FILE *config_priv_file = fopen(conf_priv, "r");
  if (config_priv_file == 0)
  {
    fprintf(stderr, "libbyz: Invalid private configuration file %s \n", conf_priv);
    return -1;
  }

  // Initialize random number generator
  srand48(getpid());

  // 1) Create Replica (a subclass of Node).
  replica = new Verifier(
      config_file,
      config_priv_file,
      mem,
      size,
      byz_pre_prepare_delay,
      false,
      exec_command_delay);

  // 2) Sets Node pointer in Node.cc.
  node = replica;

  // Register service-specific functions.
  replica->register_exec(exec);
  replica->register_nondet_choices(comp_ndet, ndet_max_len);
  return replica->used_state_bytes();
}

void Byz_modify(char *mem, int size)
{
  replica->modify(mem, size);
}

void Byz_replica_run()
{
  stats.zero_stats();
  replica->recv();
}

void _Byz_modify_index(int bindex)
{
  replica->modify_index(bindex);
}

void Byz_reset_stats()
{
  stats.zero_stats();
}

void Byz_print_stats()
{
  stats.print_stats();
}

