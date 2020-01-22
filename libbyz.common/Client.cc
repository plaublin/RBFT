#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // for TCP_NODELAY
#include <netdb.h>
#include <pthread.h>

#include "th_assert.h"
#include "Client.h"
#include "ITimer.h"
#include "Message.h"
#include "Reply.h"
#include "Request.h"
#include "Wrapped_request.h"
#include "Req_queue.h"
#include "Array.t"
#include "tcp_net.h"
#include "parameters.h"

//#define ADJUST_RTIMEOUT 1

#define NB_MAX_CLIENT_RETRANS   3

Client::Client(FILE *config_file, FILE *config_priv, short port) :
  Node(config_file, config_priv, port), t_reps(2 * f() + 1), c_reps(f() + 1)
{
  // Fail if node is is a replica.
  if (is_replica(id()))
    th_fail("Node is a replica");

  rtimeout = client_retrans_timeout; // Initial timeout value
  rtimer = new ITimer(rtimeout, rtimer_handler);

  out_rid = new_rid();
  out_req = 0;

  pthread_mutex_init(&map_of_requests_lock, NULL);

  // Multicast new key to all replicas.
  send_new_key();
  atimer->start();

#if USE_TCP_CONNECTIONS
  replicas_sockets = (int*) malloc(sizeof(int) * num_replicas);

  for (int i = 0; i < num_replicas; i++)
  {
    // create the socket
    replicas_sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
    if (replicas_sockets[i] == -1)
    {
      perror("Error while creating the socket! ");
      exit(errno);
    }

    // TCP NO DELAY
    int flag = 1;
    int result = setsockopt(replicas_sockets[i], IPPROTO_TCP, TCP_NODELAY,
        (char*) &flag, sizeof(int));
    if (result == -1)
    {
      perror("Error while setting TCP NO DELAY! ");
    }

    // re-use addr
    flag = 1;
    setsockopt(replicas_sockets[i], SOL_SOCKET, SO_REUSEADDR, &flag,
        sizeof(flag));

    // bind to the port defined in the config file
    struct sockaddr_in configured_sockaddr;
    configured_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    configured_sockaddr.sin_family = AF_INET;
    configured_sockaddr.sin_port = htons(clients_ports[id() - num_replicas]);

    if (bind(replicas_sockets[i], (struct sockaddr*) &configured_sockaddr,
        sizeof(configured_sockaddr)) == -1)
    {
      perror("Error while binding to the socket! ");
      exit(errno);
    }

    // connect to the server
    // since we have multiple NICs, use replicas_ipaddr[], and not replicas_hostname[]
    struct sockaddr_in addr;
    //struct hostent *hp = gethostbyname(replicas_hostname[i]);
    //bcopy((char*) hp->h_addr, (char*) &addr.sin_addr.s_addr, hp->h_length);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(replicas_ipaddr[i]);
    addr.sin_port = htons(replicas_ports[i]);

    while (true)
    {
      if (connect(replicas_sockets[i], (struct sockaddr*) &addr, sizeof(addr))
          < 0)
      {
        fprintf(stderr, "Client %i connecting to Verifier %i: ", id(), i);
        fprintf(stderr, "... attempting again\n");
        sleep(1);
      }
      else
      {
        fprintf(stderr, "Client %i to Verifier %i: connection successful (socket %d)!\n",
            id(), i, replicas_sockets[i]);
        break;
      }
    }
  }
#else
  replicas_sockets = NULL;
#endif
}

Client::~Client()
{
  delete rtimer;
}

void Client::reset()
{
  rtimeout = client_retrans_timeout;
}

bool Client::send_request(Request *req, bool faultyClient)
{
  Req_queue tmp; //req_queue used just to store the request, because wrapped request constructor takes a request queue as a parameter
  //It can be easily modified to accept a request* instead of a request queue, but I prefer to do exactly the same things
  //that are done for the generation of a Pre_prepare message.

  Request *cloned = req->clone();
  //cloned->re_authenticate(false);
  tmp.append(cloned);
#ifdef FAULTY_SENDS_VALID_REQ_TO_ONE_NODE
  Wrapped_request* wrapped = new Wrapped_request((Seqno) id(), (View) 1, tmp,
      false);
#else
  Wrapped_request* wrapped = new Wrapped_request((Seqno) id(), (View) 1, tmp,
      faultyClient);
#endif

  //bool mac_is_valid = Message::is_mac_valid(
  //        (Message_rep*) ((Wrapped_request*) wrapped->contents()));
  //fprintf(stderr, "Client %d sends a wrapped request with faultyClient=%s and mac_is_valid=%s\n", id(), (faultyClient ? "true" : "false"), (mac_is_valid ? "true" : "false"));

  if (out_req == 0)
  {
    if (faultyClient)
    {
#if defined(FAULTY_FOR_ALL)
      //fprintf(stderr, "FAULTY FOR ALL\n");
      send_bad_request_to_all(wrapped,faultyClient);

#elif defined(FAULTY_FOR_MASTER)
      //fprintf(stderr, "FAULTY FOR MASTER\n");
      send_bad_request_to_master_only(wrapped,faultyClient);

#elif defined(FAULTY_SENDS_VALID_REQ_TO_ONE_NODE)
      //fprintf(stderr, "FAULT SENDS VALID REQ TO ONE NODE\n");
      send_wrapped(wrapped, 0, replicas_sockets);
      faultyClient = false;

#elif defined(ATTACK1)
      // send the request with the invalid MAC to node 0 only
      send_wrapped(wrapped, 0, replicas_sockets);

      // send the request with a valid MAC to the other nodes
      Message::set_mac_valid((Message_rep*) (wrapped->contents()));
      for (int i = 1; i < num_replicas; i++) {
          send_wrapped(wrapped, i, replicas_sockets);
      }

      faultyClient = false;

#elif defined(ATTACK2)
    // send the request with the invalid MAC to all nodes but 0
    for (int i = 1; i < num_replicas; i++)
    {
      send_wrapped(wrapped, i, replicas_sockets);
    }

    Message::set_mac_valid((Message_rep*) (wrapped->contents()));
    send_wrapped(wrapped, 0, replicas_sockets);

    faultyClient = false;

#endif
    }
    else
    {
      send_wrapped(wrapped, All_replicas, replicas_sockets);
    }

    delete wrapped;

    if (!faultyClient)
    {
      out_req = req;
      need_auth = false;
      n_retrans = 0;

#ifdef ADJUST_RTIMEOUT
      // Adjust timeout to reflect average latency
      rtimer->adjust(rtimeout);

      // Start timer to measure request latency
      latency.reset();
      latency.start();
#endif

      rtimer->start();
      return true;
    }
    else
      {
      return false;
    }

  }
  else
  {
    // Another request is being processed.
    delete wrapped;
    return false;
  }
}

void Client::send_bad_request_to_all(Wrapped_request* wrapped, bool faultyClient){

  send_wrapped(wrapped, All_replicas, replicas_sockets);
}


void Client::send_bad_request_to_master_only(Wrapped_request* wrapped, bool faultyClient){
  for (int i = 0; i < num_replicas; i++)
       {
         //This faulty client sends a bad request to replica 0 and good requests to the other replicas

         if (i == 0)
         {
           Message::set_mac_unvalid((Message_rep*) (wrapped->contents()));
         }
         else
         {
           Message::set_mac_valid(((Message_rep*) wrapped->contents()));
         }
         send_wrapped(wrapped, i, replicas_sockets);
       }
}

// open loop send request operation
bool Client::send_request_open_loop(Request *req, bool faultyClient)
{
  Req_queue tmp; //req_queue used just to store the request, because wrapped request constructor takes a request queue as a parameter
  //It can be easily modified to accept a request* instead of a request queue, but I prefer to do exactly the same things
  //that are done for the generation of a Pre_prepare message.

  Request_id rid = req->request_id();

  Request *cloned = req->clone();
  //cloned->re_authenticate(false);
  tmp.append(cloned);
#ifdef FAULTY_SENDS_VALID_REQ_TO_ONE_NODE
  Wrapped_request* wrapped = new Wrapped_request((Seqno) id(), (View) 1, tmp,
      false);
#else
  Wrapped_request* wrapped = new Wrapped_request((Seqno) id(), (View) 1, tmp,
      faultyClient);
#endif

  if (faultyClient)
  {
#if defined(FAULTY_FOR_ALL)
    //fprintf(stderr, "FAULTY FOR ALL\n");
    send_bad_request_to_all(wrapped, faultyClient);

#elif defined(FAULTY_FOR_MASTER)
    //fprintf(stderr, "FAULTY FOR MASTER\n");
    send_bad_request_to_master_only(wrapped,faultyClient);

#elif defined(FAULTY_SENDS_VALID_REQ_TO_ONE_NODE)
    //fprintf(stderr, "FAULT SENDS VALID REQ TO ONE NODE\n");
    send_wrapped(wrapped, 0, replicas_sockets);
    faultyClient = false;

#elif defined(ATTACK1)
    // send the request with the invalid MAC to node 0 only
    send_wrapped(wrapped, 0, replicas_sockets);

    // send the request with a valid MAC to the other nodes
    Message::set_mac_valid((Message_rep*) (wrapped->contents()));
    for (int i = 1; i < num_replicas; i++)
    {
      send_wrapped(wrapped, i, replicas_sockets);
    }

    faultyClient = false;

#elif defined(ATTACK2)
    // send the request with the invalid MAC to all nodes but 0
    for (int i = 1; i < num_replicas; i++)
    {
      send_wrapped(wrapped, i, replicas_sockets);
    }

    Message::set_mac_valid((Message_rep*) (wrapped->contents()));
    send_wrapped(wrapped, 0, replicas_sockets);

    faultyClient = false;
#endif
  }
  else
  {
#ifdef MSG_DEBUG
    __sync_fetch_and_add(&nb_sent_requests, 1);
#endif
    send_wrapped(wrapped, All_replicas, replicas_sockets);
  }

#ifdef MSG_DEBUG
  fprintf(stderr, "Client %d sends request %qd\n", id(), rid);
#endif

#ifdef MSG_DEBUG
  if (__sync_fetch_and_add(&nb_sent_requests, 0) == 10000)
  {
    unsigned long nbs = __sync_fetch_and_and(&nb_sent_requests, 0);
    unsigned long nbr = __sync_fetch_and_and(&nb_retransmitted_requests, 0);

    fprintf(stderr, "%lu sent reqs, %lu retrans reqs, retrans perc= %f%%\n",
        nbs, nbr, nbr * 100.0 / (nbs + nbr));
  }
#endif

  Open_loop_request *olr = new Open_loop_request(wrapped);
  pthread_mutex_lock(&map_of_requests_lock);
  map_of_requests[rid] = olr;
  pthread_mutex_unlock(&map_of_requests_lock);

  // we need a new rid as we operate in an open loop
  out_rid = new_rid();

  if (!faultyClient)
  {
    need_auth = false;

#ifdef ADJUST_RTIMEOUT
    // Adjust timeout to reflect average latency
    rtimer->adjust(rtimeout);

    // Start timer to measure request latency
    latency.reset();
    latency.start();
#endif

    rtimer->start();

    return true;
  }
  else
  {
    return false;
  }
}


// send invalid requests of 9kB to the system
void Client::flood_nodes(void) {
    Message *m = new Message(Wrapped_request_tag, Max_message_size);
    Message::set_mac_unvalid((Message_rep*)(m->contents()));

    while (1) {
        send_wrapped(m, All_replicas, replicas_sockets);
    }

    delete m;
}

Message* Client::recvUDP()
{
  Message *m = 0;
  struct timeval timeout;
  fd_set fdset;

  while (true)
  {
    // select
    FD_ZERO(&fdset);
    FD_SET(sock, &fdset);

    timeout.tv_sec = 0;
    timeout.tv_usec = 100000; // 20000 is the argument of Node::hasMessages(to)

    int ret = select(sock + 1, &fdset, 0, 0, &timeout);
    if (ret > 0 && FD_ISSET(sock, &fdset))
    {
      m = new Message(Max_message_size);
      recvfrom(sock, m->contents(), m->msize(), 0, 0, 0);

      //int ret = msg_rep_size + msg_size;
      ITimer::handle_timeouts();

      return m;
    }

    ITimer::handle_timeouts();
  }

  return m;
}

Message* Client::recvTCP()
{
  Message *m = 0;
  struct timeval timeout;
  fd_set fdset;

  while (true)
  {
    // select
    FD_ZERO(&fdset);
    int maxsock = 0;

    for (int i = 0; i < num_replicas; i++)
    {
      FD_SET(replicas_sockets[i], &fdset);
      maxsock = MAX(maxsock, replicas_sockets[i]);
    }

    timeout.tv_sec = 0;
    timeout.tv_usec = 100000; // 20000 is the argument of Node::hasMessages(to)

    int ret = select(maxsock + 1, &fdset, 0, 0, &timeout);
    if (ret > 0)
    {
      for (int i = 0; i < num_replicas; i++)
      {
        if (FD_ISSET(replicas_sockets[i], &fdset))
        {
          m = new Message(Max_message_size);

          // 1) first of all, we need to receive the Message_rep (in order to get the message size)
          int msg_rep_size = recvMsg(replicas_sockets[i],
              (void*) m->contents(), sizeof(Message_rep));

          // 2) now that we have the size of the message, receive the content
          recvMsg(replicas_sockets[i],
              (void*) ((char*) m->contents() + msg_rep_size),
              m->size() - msg_rep_size);

          //int ret = msg_rep_size + msg_size;
          ITimer::handle_timeouts();

          return m;
        }
      }
    }

    ITimer::handle_timeouts();
  }

  return m;
}

Reply *Client::recv_reply()
{
  if (out_req == 0)
    // Nothing to wait for.
    return 0;

  //
  // Wait for reply
  //
  while (1)
  {
    Message* m = 0;
#if USE_TCP_CONNECTIONS
    m = recvTCP();
#else
    m = recvUDP();
#endif

#ifdef MSG_DEBUG
    fprintf(stderr, "Client %i has received a message: tag=%i\n", id(), m->tag());
#endif

    Reply* rep;
    if (!Reply::convert(m, rep) || rep->request_id() != out_rid)
    {
      delete m;
      continue;
    }

    Certificate<Reply> &reps = (rep->is_tentative()) ? t_reps : c_reps;
    if (reps.is_complete())
    {
      // We have a complete certificate without a full reply.
      if (!rep->full() || !rep->verify() || !rep->match(reps.cvalue()))
      {
        delete rep;
        continue;
      }
    }
    else
    {
      reps.add(rep);
      rep = (reps.is_complete() && reps.cvalue()->full()) ? reps.cvalue_clear()
          : 0;
    }

    if (rep)
    {
      out_rid = new_rid();
      rtimer->stop();
      out_req = 0;
      t_reps.clear();
      c_reps.clear();

      // Choose view in returned rep. TODO: could make performance
      // more robust to attacks by picking the median view in the
      // certificate.
      v = rep->view();
      cur_primary = v % num_replicas;

#ifdef ADJUST_RTIMEOUT
      latency.stop();
      rtimeout = (3*rtimeout+
          latency.elapsed()*Rtimeout_mult/(clock_mhz*1000))/4+1;
#endif

      return rep;
    }
  }
}


// recv reply. If there is no reply, wait for at most to usec.
// Used to empty the TCP buffer.
// return the number of received messages
int Client::recv_reply_noblock(long to) {
#if USE_TCP_CONNECTIONS
  int n = 0;
  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = to;
  fd_set fdset;

  // select
  FD_ZERO(&fdset);
  int maxsock = 0;

  for (int i = 0; i < num_replicas; i++)
  {
    FD_SET(replicas_sockets[i], &fdset);
    maxsock = MAX(maxsock, replicas_sockets[i]);
  }

  int ret = select(maxsock + 1, &fdset, 0, 0, &timeout);
  if (ret > 0)
  {
    for (int i = 0; i < num_replicas; i++)
    {
      if (FD_ISSET(replicas_sockets[i], &fdset))
      {
        Message *m = new Message(Max_message_size);

        // 1) first of all, we need to receive the Message_rep (in order to get the message size)
        int msg_rep_size = recvMsg(replicas_sockets[i],
            (void*) m->contents(), sizeof(Message_rep));

        // 2) now that we have the size of the message, receive the content
        recvMsg(replicas_sockets[i],
            (void*) ((char*) m->contents() + msg_rep_size), m->size()
                - msg_rep_size);

        n++;
        delete m;
      }
    }
  }

  return n;
#else
  return 0;
#endif
}



// open loop recv reply operation
Reply* Client::recv_reply_open_loop(void) {
  while (1)
  {
    Message* m = 0;
#if USE_TCP_CONNECTIONS
    m = recvTCP();
#else
    m = recvUDP();
#endif

#ifdef MSG_DEBUG
    //fprintf(stderr, "Client %i has received a message: tag=%i\n", id(), m->tag());
#endif

    Reply* rep;
    if (!Reply::convert(m, rep))
    {
      delete m;
      continue;
    }

    pthread_mutex_lock(&map_of_requests_lock);
    // First, check if this is a message saying that we can forget all the replies with an rid lower
    // than this one
    if (rep->view() == -1)
    {
#ifdef MSG_DEBUG
      fprintf(stderr, "Client %d: forget requests up to %qd\n", id(), rep->request_id()-1);
#endif
      for (std::map<Request_id, Open_loop_request*>::iterator it =
          map_of_requests.begin(); it != map_of_requests.end()
          && (Request_id)(it->second->w->seqno()) < rep->request_id(); )
      {
        delete it->second;
        map_of_requests.erase(it++);
      }
    }

    std::map<Request_id, Open_loop_request*>::iterator got = map_of_requests.find(rep->request_id());
    if (got == map_of_requests.end()) {
      pthread_mutex_unlock(&map_of_requests_lock);
      delete rep;
      continue;
    }

#ifdef MSG_DEBUG
    fprintf(stderr, "Client %d recv reply for request %qd\n", id(), got->first);
#endif

    Certificate<Reply> *reps;
    if (rep->is_tentative()) {
      reps = &got->second->t_reps;
    } else {
      reps = &got->second->c_reps;
    }

    if (reps->is_complete())
    {
      // We have a complete certificate without a full reply.
      if (!rep->full() || !rep->verify() || !rep->match(reps->cvalue()))
      {
        delete rep;
        continue;
      }
    }
    else
    {
      reps->add(rep);
      rep = (reps->is_complete() && reps->cvalue()->full()) ? reps->cvalue_clear()
          : 0;
    }

    if (rep)
    {
#ifdef MSG_DEBUG
    fprintf(stderr, "Client %d accepting reply for request %qd\n", id(), got->first);
#endif

      delete got->second;
      map_of_requests.erase(got);
      pthread_mutex_unlock(&map_of_requests_lock);

      // Choose view in returned rep. TODO: could make performance
      // more robust to attacks by picking the median view in the
      // certificate.
      v = rep->view();
      cur_primary = v % num_replicas;

#ifdef ADJUST_RTIMEOUT
      latency.stop();
      rtimeout = (3*rtimeout+
          latency.elapsed()*Rtimeout_mult/(clock_mhz*1000))/4+1;
#endif

      return rep;
    } else {
      pthread_mutex_unlock(&map_of_requests_lock);
    }
  }
}

void rtimer_handler()
{
  th_assert(node, "Client is not initialized");
  ((Client*) node)->retransmit();
}

void Client::retransmit()
{
  // Retransmit any outstanding request.
  static const int thresh = 1;
  static const int nk_thresh = 4;
  static const int nk_thresh_1 = 100;

  if (out_req != 0)
  {
    INCR_OP( req_retrans);

    //    fprintf(stderr, ".");
    n_retrans++;

    if (n_retrans == nk_thresh || n_retrans % nk_thresh_1 == 0)
    {
      send_new_key();
    }

    bool ro = out_req->is_read_only();
    bool change = (ro || out_req->replier() >= 0) && n_retrans > thresh;
    //    fprintf(stderr, "%d %d %d %d\n", id(), n_retrans, ro, out_req->replier());

    if (need_auth || change)
    {
      // Compute new authenticator for request
      out_req->re_authenticate(change);
      need_auth = false;
      if (ro && change)
        t_reps.clear();
    }

    Req_queue tmp;
    Request* cloned = out_req->clone();
    tmp.append(cloned);
    Wrapped_request* wrapped = new Wrapped_request((Seqno) id(), (View) 1, tmp);

#ifdef MSG_DEBUG
    int tag =((Message_rep*) (wrapped->contents()))->tag;
    fprintf(stderr, "Client %i is retransmitting request %qd, tag is: %i\n", id(), out_req->request_id(),tag);
#endif

    send_wrapped(wrapped, All_replicas, replicas_sockets);

    delete wrapped;
  }

  // Code triggered only in open-loop: retransmit all the requests for which I do not have a reply
  pthread_mutex_lock(&map_of_requests_lock);
  for (std::map<Request_id, Open_loop_request*>::iterator it = map_of_requests.begin(); it != map_of_requests.end(); ) {
    if (++(it->second->n_retrans) < NB_MAX_CLIENT_RETRANS) {
#ifdef MSG_DEBUG
      fprintf(stderr, "Client %d is retransmitting request %qd\n", id(), it->first);
#endif
      Wrapped_request* wrapped = it->second->w;
      send_wrapped(wrapped, All_replicas, replicas_sockets);
      it++;
    } else {
      delete it->second;
      map_of_requests.erase(it++);
    }
  }
  pthread_mutex_unlock(&map_of_requests_lock);


#ifdef ADJUST_RTIMEOUT
  // exponential back off
  if (rtimeout < Min_rtimeout) rtimeout = 100;
  rtimeout = rtimeout+lrand48()%rtimeout;
  if (rtimeout > Max_rtimeout) rtimeout = Max_rtimeout;
  rtimer->adjust(rtimeout);
#endif

  rtimer->restart();
}

void Client::send_new_key()
{
  Node::send_new_key();
  need_auth = true;

  // Cleanup reply messages authenticated with old keys.
  t_reps.clear();
  c_reps.clear();
}
