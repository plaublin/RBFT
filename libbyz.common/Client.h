#ifndef _Client_h
#define _Client_h 1

#include <stdio.h>
#include "types.h"
#include "Node.h"
#include "Certificate.h"
#include "Array.h"
#include <time.h>
#include <map>
#include "Wrapped_request.h"

class Reply;
class Request;
class ITimer;
extern void rtimer_handler();

/*
 * An open loop request is the (wrapped) request + the
 * certificates. It is created when a request is sent
 * to the nodes and removed when the reply has been accepted.
 */
class Open_loop_request
{
public:
  Open_loop_request(Wrapped_request *wr) {
    w=wr;
    n_retrans = 0;
    t_reps.clear();
    c_reps.clear();
  }

  ~Open_loop_request(void) {
    delete w;
    t_reps.clear();
    c_reps.clear();
  }

  Wrapped_request *w;
  Certificate<Reply> t_reps; // Certificate with tentative replies (size 2f+1)
  Certificate<Reply> c_reps; // Certificate with committed replies (size f+1)
  int n_retrans;             // number of times this request has been retransmitted
};

class Client: public Node
{
public:
  Client(FILE *config_file, FILE *config_priv, short port = 0);
  // Effects: Creates a new Client object using the information in
  // "config_file" and "config_priv". The line of config assigned to
  // this client is the first one with the right host address (if
  // port==0) or the first with the right host address and port equal
  // to "port".

  virtual ~Client();
  // Effects: Deallocates all storage associated with this.

  bool send_request(Request *req, bool faultyClient = false);
  // Effects: Sends request m to the service. Returns FALSE iff two
  // consecutive request were made without waiting for a reply between
  // them.
  // If faultyClient == true, the MAC of the wrapped request is corrupted

  void send_wrapped(Message *m, int i, int* sockets);
  // wrapped send: uses either TCP or UDP. For the client only.

  void send_bad_request_to_all(Wrapped_request* wrapped, bool faultyClient);
  void send_bad_request_to_master_only(Wrapped_request* wrapped, bool faultyClient);

  // open loop send request operation
  bool send_request_open_loop(Request *req, bool faultyClient = false);

  // send invalid requests of 9kB to the system
  void flood_nodes(void);

  Reply *recv_reply();
  // Effects: Blocks until it receives enough reply messages for
  // the previous request. returns a pointer to the reply. The caller is
  // responsible for deallocating the request and reply messages.

  // recv reply. If there is no reply, wait for at most to usec.
  // Used to empty the TCP buffer.
  // return the number of received messages
  int recv_reply_noblock(long to);

  // open loop recv reply operation
  Reply* recv_reply_open_loop(void);

  Request_id get_rid() const;
  // Effects: Returns the current outstanding request identifier. The request
  // identifier is updated to a new value when the previous message is
  // delivered to the user.

  void set_rid(Request_id rid);


  void reset();
  // Effects: Resets client state to ensure independence of experimental
  // points.
  // More precisely, resets the timeout rtimeout to its initial value.

private:
  Request *out_req; // Outstanding request
  bool need_auth; // Whether to compute new authenticator for out_req
  Request_id out_rid; // Identifier of the outstanding request
  int n_retrans; // Number of retransmissions of out_req
  int rtimeout; // Timeout period in msecs

  // Maximum retransmission timeout in msecs
  static const int Max_rtimeout = 10000;

  // Minimum retransmission timeout after retransmission
  // in msecs
  static const int Min_rtimeout = 10;

  Cycle_counter latency; // Used to measure latency.

  // Multiplier used to obtain retransmission timeout from avg_latency
  static const int Rtimeout_mult = 4;

  Certificate<Reply> t_reps; // Certificate with tentative replies (size 2f+1)
  Certificate<Reply> c_reps; // Certificate with committed replies (size f+1)

  // Open loop
  pthread_mutex_t map_of_requests_lock; // lock for the map_of_requests
  std::map<Request_id, Open_loop_request*> map_of_requests;

  friend void rtimer_handler();
  ITimer *rtimer; // Retransmission timer

  void retransmit();
  // Effects: Retransmits any outstanding request and last new-key message.

  void send_new_key();
  // Effects: Calls Node's send_new_key, and cleans up stale replies in
  // certificates.

  Message* recvTCP();
  // Receive a message using the TCP sockets

  Message* recvUDP();
  // Receive a message using the UDP socket

  // sockets for communications with replicas
  int *replicas_sockets;

  unsigned long nb_sent_requests;
  unsigned long nb_retransmitted_requests;
};

inline Request_id Client::get_rid() const
{
  return out_rid;
}

inline void Client::set_rid(Request_id rid)
{
  out_rid = rid;
}

inline void Client::send_wrapped(Message *m, int i, int* sockets) {
#if USE_TCP_CONNECTIONS
  sendTCP(m, i, sockets);
#else
  sendUDP(m, i);
#endif
}
#endif // _Client_h
