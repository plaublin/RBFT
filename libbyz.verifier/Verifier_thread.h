#ifndef _Verifier_thread_h
#define _Verifier_thread_h 1

#include <time.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h> //for non_blocking socket
#include <errno.h>

#include "th_assert.h"
#include "Message_tags.h"
#include "ITimer.h"
#include "Request.h"
#include "Pre_prepare.h"
#include "Prepare.h"
#include "Commit.h"
#include "Checkpoint.h"
#include "New_key.h"
#include "Status.h"
#include "Fetch.h"
#include "Data.h"
#include "Meta_data.h"
#include "Meta_data_d.h"
#include "View_change.h"
#include "View_change_ack.h"
#include "New_view.h"
#include "Principal.h"
#include "Prepared_cert.h"
#include "Reply.h"
#include "K_max.h"
#include "Verifier.h"

class Node_blacklister;

class Verifier_thread
{
public:
  Verifier_thread() {}

  void *run(void);

  int client_socket; //read client request from there

private:
  void init_comm_with_clients(void);

  void handle(Wrapped_request* m);

  /* the socket on which the replica listens for connections from incoming clients */
  int bootstrap_clients_socket;
  struct sockaddr_in bootstrap_clients_sin;

  struct timeval select_timeout;
  fd_set fdset;
  Node_blacklister* client_blacklister;

  unsigned long nb_valid;
  unsigned long nb_non_valid;
};

extern "C" void *Verifier_thread_startup(void *);

#endif //_Verifier_thread_h
