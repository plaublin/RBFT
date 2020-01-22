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

#include "Verifier.h"
#include "Node_blacklister.h"
#include "Propagate.h"

class Message;
class Request;
class Bitmap;
class Propagate;
class Digest;
class Node_blacklister;

class Forwarding_thread
{

public:
  Forwarding_thread() {}

  void *run(void);

private:
  // req is a valid request. This method does not delete req.
  void handle(Request *req);

  // req is a valid request. This method may delete req.
  //void handle(Request *req, int src);

  // pr is a valid propagate. This method may delete pr.
  void handle(Propagate *pr, int src);

  // update the missing_reqs structure, shared with the verifier thread
  bool update_missing_reqs(int cid, Request_id rid);

  // add request req received from node node_id to the certificate cert.
  // return true if a new certificate has been created, false otherwise
  bool add_request_to_certificate(Request *req, int node_id);

  // Read a message from socket sock and return it (or 0 in case of error).
  Message *get_message_from_forwarder(int sock);

  // flooding attack
  void run_flood_attack(void);

  void send_propagate(Propagate *pr);
  void load_balancing_send(Message *m, int i);

  // retransmission of Propagate
  void exec_retrans_timer(void);
  void retransmit(void);

  // send the Propagate if enough time has been elapsed since the last send
  void send_propagate_if_needed(void);

  Node_blacklister* node_blacklister;
#ifdef LOAD_BALANCING_SEND
  int* send_to_master_nic;
#endif
  Time retrans_period;
  Time retrans_deadline;

  Time last_propagate_send;
  Propagate* zepropagate;

  unsigned long long c_retrans;
};

// send the propagate to all the nodes but myself
inline void Forwarding_thread::send_propagate(Propagate *pr)
{
#ifdef DEBUG_PERIODICALLY_PRINT_THR
    __sync_fetch_and_add (&replica->nb_sent_propagate, 1);
#endif

  for (int x = 0; x < replica->num_replicas; x++)
  {
    if (x != replica->id() && !node_blacklister->is_blacklisted(x))
    {
      load_balancing_send((Message *) pr, x);
    }
  }
}

inline void Forwarding_thread::load_balancing_send(Message * m, int x)
{
#ifdef LOAD_BALANCING_SEND

#if USE_TCP_CONNECTIONS
  replica->sendTCP(m, x, (send_to_master_nic[x] ? replica->snd_socks : replica->snd_socks_lb));
#else
  replica->sendUDP(m, x);
#endif
  send_to_master_nic[x] = !send_to_master_nic[x];

#else

#if USE_TCP_CONNECTIONS
  replica->sendTCP(m, x, replica->snd_socks);
#else
  replica->sendUDP(m, x);
#endif

#endif
}

extern "C" void *Forwarding_thread_startup(void *);
