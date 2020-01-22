#ifndef _Replica_h
#define _Replica_h 1

#include <unordered_map>
#include <map>

#include "State_defs.h"
#include "types.h"
#include "Req_queue.h"
#include "Log.h"
#include "Set.h"
#include "Certificate.h"
#include "Prepared_cert.h"
#include "View_info.h"
#include "Rep_info.h"
#include "Partition.h"
#include "Digest.h"
#include "Node.h"
#include "State.h"
#include "Big_req_table.h"
#include "libbyz.h"
#include "Request.h"
#include "Wrapped_request.h"
#include "parameters.h"
#include "Verifier_stats.h"
#include "Blocking_circular_buffer.h"
#include "Circular_buffer.h"

//#ifdef ADAPTIVE
//#include "Time.h" // why ?
//#endif

class Request;
class Reply;
class Pre_prepare;
class Prepare;
class Commit;
class Checkpoint;
class Status;
class View_change;
class New_view;
class New_key;
class Fetch;
class Data;
class Meta_data;
class Meta_data_d;
class Reply;
class Query_stable;
class Reply_stable;
class Protocol_instance_change;
class Propagate;

extern void delay_pre_prepare_timer_handler();

#ifdef PERIODICALLY_MEASURE_THROUGHPUT
extern void periodic_thr_measure_handler();
#endif

extern void vtimer_handler();
extern void stimer_handler();
extern void ntimer_handler();
extern void statTimer_handler();

#ifdef DEBUG_PERIODICALLY_PRINT_THR
extern void pipes_thr_handler();
#endif

#define ALIGNMENT_BYTES 2

typedef struct forwarding_cert
{
  Request_id req_id;
  Digest d;
  Request *r;
  Bitmap* fwd_bitmap;
} forwarding_cert;

void print_statistics_before_dying(int sig);

class Verifier: public Node
{
public:

  Verifier(FILE *config_file, FILE *config_priv, char *mem, int nbytes,
      int byz_pre_prepare_delay = 0, bool small_batches = false,
      long int exec_command_delay = 0);
  // Requires: "mem" is vm page aligned and nbytes is a multiple of the
  // vm page size.
  // Effects: Create a new server replica using the information in
  // "config_file" and "config_priv". The replica's state is set to the
  // "nbytes" of memory starting at "mem".

  virtual ~Verifier();
  // Effects: Kill server replica and deallocate associated storage.

  // Init the connection with the replicas
  void init_comm_with_replicas(void);

  // Init the connection with the verifier
  void init_comm_with_verifier(void);

  void recv();
  // Effects: Loops receiving messages. if idle, may dequeue status and fetch messages and dispatch
  // them to the appropriate handlers


  // verify a request coming from a Client
  Request* verify(Wrapped_request* m);

  void jump_table(Message *m);
  //dispatches m to the appropriate handler (or queue)

  // from the available requests in rqueue, send a batch to the PIRs);
  void send_request_to_PIRs(Request *req);

  void send(Message *m, int i);
  // used to send a message using the appropriate socket

  // Methods to register service specific functions. The expected
  // specifications for the functions are defined below.
  void register_exec(
      int(*e)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool, long int));
  // Effects: Registers "e" as the exec_command function.

  void register_nondet_choices(void(*n)(Seqno, Byz_buffer *), int max_len);
  // Effects: Registers "n" as the non_det_choices function.

  void compute_non_det(Seqno n, char *b, int *b_len);
  // Requires: "b" points to "*b_len" bytes.
  // Effects: Computes non-deterministic choices for sequence number
  // "n", places them in the array pointed to by "b" and returns their
  // size in "*b_len".

  int max_nd_bytes() const;
  // Effects: Returns the maximum length in bytes of the choices
  // computed by compute_non_det

  int used_state_bytes() const;
  // Effects: Returns the number of bytes used up to store protocol
  // information.

  void modify(char *mem, int size);
  // Effects: Informs the system that the memory region that starts at
  // "mem" and has length "size" bytes is about to be modified.


  void modify_index(int bindex);
  // Effects: Informs the system that the memory page with index
  // "bindex" is about to be modified.

  void process_new_view(Seqno min, Digest d, Seqno max, Seqno ms);
  // Effects: Update replica's state to reflect a new-view: "min" is
  // the sequence number of the checkpoint propagated by new-view
  // message; "d" is its digest; "max" is the maximum sequence number
  // of a propagated request +1; and "ms" is the maximum sequence
  // number known to be stable.

  void send_view_change();
  // Effects: Send view-change message.

  void send_status(bool force = false);
  // Effects: Sends a status message.
  // [elwong] Now takes a parameter that can force the message out
  //  regardless of when the last message was sent.

  bool has_req(int cid, const Digest &d);
  // Effects: Returns true iff there is a request from client "cid"
  // buffered with operation digest "d". XXXnot great

  bool delay_vc();
  // Effects: Returns true iff view change should be delayed.

  Big_req_table* big_reqs();
  // Effects: Returns the replica's big request table.

  // Verify the Protocol Instance Change.
  // Return true if it is valid, false otherwise
  bool verify_protocol_instance_change(Protocol_instance_change* m);

  // Read all remaining messages in the channel between the replicas and the verifier
  void read_all_remaining_messages(void);

#ifdef FAIRNESS_ADAPTIVE
  unsigned long get_req_count();
  //  returns req_count

  void increment_req_count();
  //  req_count++;
#endif

  Blocking_circular_buffer *verifier_to_executor_buffer;
  Circular_buffer *verifier_thr_to_fwd_thr_buffer;
  Circular_buffer *request_buffer;

  Request_id *min_verified_rid;
  Request_id *max_verified_rid;
  std::map<Request_id, char> *missing_reqs;
  pthread_mutex_t *missing_reqs_lock; // 1 lock per client

  std::unordered_map<Request_id, forwarding_cert>* certificates;
  pthread_mutex_t *certificates_lock; // 1 lock per client

  Request_id *min_exec_rid;
  std::map<Request_id, Reply*> *last_replies;

  // kzimp file descriptors
  int verifier_to_pirs_fd;
  int *pirs_to_verifier_fd;

  /* the socket on which the replica listens for connections from incoming clients */
  int clients_bootstrap_socket;
  struct sockaddr_in clients_bootstrap_sin;
  /* the list of sockets for connections with incoming clients */
  int *clients_sockets_fds;

  bool* excluded_replicas; //array of bool. if excluded_replicas[i] == true, replica i is not added to the socket i listen in select
  bool excluded_clients;

  int client_socket; //read client request from there
  // int client_port;
  //int interval;
  // char* message;
  bool* blacklisted; //this will point to an array of num_principals bools, initialized at 0
  //client with id client_id is blacklisted iff blacklisted[client_id]==true
  Addr client_address;
  Verifier_stats *statsComputer;

  long nb_monitoring_since_last_pic;

  int nb_retransmissions;
  long nb_executed;
  float average_batch_size;
  int sum_batch_size;
  int nb_batch_sent;

#ifdef PERIODICALLY_MEASURE_THROUGHPUT
  ITimer *periodic_thr_measure;
  int nb_requests_4_periodic_thr_measure;
  long long start_cycle_4_periodic_thr_measure;
  float measured_throughput[MAX_NB_MEASURES];
  int next_measure_idx;
#endif

  // time at which the statTimer has been called for the first time, in usec
  long long statTimer_first_call;

  Time send_to_replica_start;
  Time recv_ordered_req_start;
  int nb_sent_to_replica_req;
  int nb_recv_ordered_req;

  // Last replies sent to each principal.
  Rep_info replies;
  void send_new_key();
  // Effects: Calls Node's send_new_key, adjusts timer and cleans up
  // stale messages.

  //SONIA variables for communication between verifiers
  int *snd_socks;
  int *rcv_socks;

  //PL variables for communication between verifiers -- load balancing
  int *snd_socks_lb;
  int *rcv_socks_lb;

  int bootstrap_socket;
  struct sockaddr_in bootstrap_sin;

  // time (in cycles) at which the first request has been received
  Time first_request_time;

#ifdef DEBUG_PERIODICALLY_PRINT_THR
  // Timer for periodically printing the throughput of the different pipes
  ITimer *pipes_thr_timer;
  Time pipes_thr_start;
  int nb_received_requests;
  int nb_sent_requests_to_forwarder;
  int nb_recv_requests_from_verifier_thr;
  int nb_sent_propagate;
  int *nb_recv_propagate; // 1 value for each node
  int nb_sent_requests_to_verifier;
  int nb_recv_requests_from_forwarder;
  int nb_sent_requests_to_replicas;
  int *nb_recv_requests_from_replicas; // 1 value for each replica
  int nb_sent_requests_to_exec;
  int nb_recv_requests_from_verifier;
  int nb_sent_replies;
#endif

private:
  friend class State;
  friend class Wrapped_request;

  //
  // Message handlers:
  //
  void handle(Request* m);
  void handle(Wrapped_request *m, int instance_id);
  void handle(Pre_prepare* m);
  void handle(Prepare* m);
  void handle(Commit* m);
  void handle(Checkpoint* m);
  void handle(View_change* m);
  void handle(New_view* m);
  void handle(View_change_ack* m);
  void handle(Status* m);
  void handle(New_key* m);
  void handle(Fetch* m);
  void handle(Data* m);
  void handle(Meta_data* m);
  void handle(Meta_data_d* m);
  void handle(Reply* m, bool mine = false);
  void handle(Query_stable* m);
  void handle(Reply_stable* m);
  void handle(Protocol_instance_change* m);
  // Effects: Execute the protocol steps associated with the arrival
  // of the argument message.

  void create_and_send_protocol_instance_change(void);

  friend void delay_pre_prepare_timer_handler();

  friend void vtimer_handler();
  friend void stimer_handler();
  friend void ntimer_handler();
  friend void statTimer_handler();
  //friend void cb_monitoring_timer_handler();
  // Effects: Handle timeouts of corresponding timers.

  //
  // Auxiliary methods used by primary to send messages to the replica
  // group:
  //
  void send_pre_prepare(bool force_send = false);
  // Effects: sends a pre_prepare message.  If force_send is true then
  // sends a pre_prepare message no matter what.  If force_send is
  // false then sends a pre_prepare message only the batch is full


  void send_prepare(Prepared_cert& pc);
  // Effects: Sends a prepare message if appropriate.

  void send_commit(Seqno s);

  void send_null();
  // Send a pre-prepare with a null request if the system is idle

  //
  // Miscellaneous:
  //
  bool execute_read_only(Request *m);
  // Effects: If some request that was tentatively executed did not
  // commit yet (i.e. last_tentative_execute < last_executed), returns
  // false.  Otherwise, returns true, executes the command in request
  // "m" (provided it is really read-only and does not require
  // non-deterministic choices), and sends a reply to the client

  void execute_committed();
  // Effects: Executes as many commands as possible by calling
  // execute_prepared; sends Checkpoint messages when needed and
  // manipulates the wait timer.

  void execute_prepared(bool committed = false);
  // Effects: Tentatively executes as many commands as possible. It
  // extracts requests to execute commands from a message "m"; calls
  // exec_command for each command; and sends back replies to the
  // client. The replies are tentative unless "committed" is true.

  void mark_stable(Seqno seqno, bool have_state);
  // Requires: Checkpoint with sequence number "seqno" is stable.
  // Effects: Marks it as stable and garbage collects information.
  // "have_state" should be true iff the replica has a the stable
  // checkpoint.

  void new_state(Seqno seqno);
  // Effects: Updates this to reflect that the checkpoint with
  // sequence number "seqno" was fetch.

  Pre_prepare *prepared(Seqno s);
  // Effects: Returns non-zero iff there is a pre-prepare pp that prepared for
  // sequence number "s" (in this case it returns pp).

  Pre_prepare *committed(Seqno s);
  // Effects: Returns non-zero iff there is a pre-prepare pp that committed for
  // sequence number "s" (in this case it returns pp).

  bool has_new_view() const;
  // Effects: Returns true iff the replica has complete new-view
  // information for the current view.

  template<class T> bool in_w(T *m);
  // Effects: Returns true iff the message "m" has a sequence number greater
  // than last_stable and less than or equal to last_stable+max_out.

  template<class T> bool in_wv(T *m);
  // Effects: Returns true iff "in_w(m)" and "m" has the current view.

  template<class T> void gen_handle(Message *m);
  // Effects: Handles generic messages.

  template<class T> void retransmit(T *m, Time &cur, Time *tsent, Principal *p);
  // Effects: Retransmits message m (and re-authenticates it) if
  // needed. cur should be the current time.

  bool retransmit_rep(Reply *m, Time &cur, Time *tsent, Principal *p);

  // void print_xp_results(int sig);


  void update_max_rec();
  // Effects: If max_rec_n is different from the maximum sequence
  // number for a recovery request in the state, updates it to have
  // that value and changes keys. Otherwise, does nothing.

  Message* pick_next_status();

  //
  // Instance variables:
  //
  Seqno seqno; // Sequence number to attribute to next protocol message,
  // only valid if I am the primary.

  struct protocol_instance_change_quorum
  {
    int nb_msg;
    View protocol_instance_id;
    Bitmap* bitmap;
  };

  // the quorum of Protocol_instance_change messages
  struct protocol_instance_change_quorum pic_quorum;

  // current protocol instance
  View current_protocol_instance;

  /* The socket to listen for connection for PIRs. */
  int verifier_boostrap_socket;
  struct sockaddr_in verifier_bootstrap_sin;

  // to send to PIRs in a round-robin fashion
  int rr_last_recv;

  // for statistics
  //Verifier_stats* verif_stats;

  // for each client, the last request that has been received
  Request **last_received_request;

  // for each PIR and each client, the last reply that has been received
  Reply ***last_received_reply;

  // Logging variables used to measure average batch size
  int nbreqs; // The number of requests executed in current interval
  int nbrounds; // The number of rounds of BFT executed in current interval

  Seqno last_stable; // Sequence number of last stable state.
  Seqno low_bound; // Low bound on request sequence numbers that may
  // be accepted in current view.
public:
  Seqno last_prepared; // Sequence number of highest prepared request
  Seqno last_executed; // Sequence number of last executed message.
  Seqno last_tentative_execute; // Sequence number of last message tentatively
  // executed.
  int
  (*exec_command)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool, long int);
  long int exec_command_delay; //delay caused by a request execution.
  Seqno max_rec_n; // Maximum sequence number of a recovery request in state.

  /*  int nothing_to_read;
   int something_to_read;

   int nothing_to_read_fwd;
   int something_to_read_fwd;*/
private:

  // Sets and logs to keep track of messages received. Their size
  // is equal to max_out.
  Req_queue rqueue; // For read-write requests.
  Req_queue ro_rqueue; // For read-only requests

  Log<Prepared_cert> plog;

  Big_req_table brt; // Table with big requests
  friend class Big_req_table;

  Log<Certificate<Commit> > clog;
  Log<Certificate<Checkpoint> > elog;

  // Set of stable checkpoint messages above my window.
  Set<Checkpoint> sset;

  // State abstraction manages state checkpointing and digesting
  State state;

  ITimer *stimer; // Timer to send status messages periodically.
  Time last_status; // Time when last status message was sent

  //
  // View changes:
  //
  View_info vi; // View-info abstraction manages information about view changes
  ITimer *vtimer; // View change timer

  //Timer for computing statistics
  ITimer *statTimer;

  //Timer for monitoring the in/out throughput of circular buffers.
  //ITimer *cb_monitoring_timer;


  bool need_requests; // flag if we need to gather client requests

  // the following timer is there to delay sending of pre_prepares
  ITimer *delay_pre_prepare_timer;

  bool limbo; // True iff moved to new view but did not start vtimer yet.
  bool has_nv_state; // True iff replica's last_stable is sufficient
  // to start processing requests in new view.
  bool status_messages_badly_needed;
  Seqno not_deprioritize_status_before_this;

  //
  // Recovery
  //
  bool rec_ready; // True iff replica is ready to recover
  bool recovering; // True iff replica is recovering.
  bool vc_recovering; // True iff replica exited limbo for a view after it started recovery
  bool corrupt; // True iff replica's data was found to be corrupt.

  ITimer* ntimer; // Timer to trigger transmission of null requests when system is idle
  /*  fd_set file_descriptors; //set of file descriptors to listen to (only one socket, in this case)
   timeval listen_time; //max time to wait for something readable in the file descriptors */
  // non-blocking socket listening
  // (return earlier if something becames readable)

  Message** status_pending; //array used to store pointers of peding status messages
  int status_to_process; //index to the next status to process (round robin)
  int s_identity; //just an index...

  long int byz_pre_prepare_delay;
  bool small_batches;

  // Estimation of the maximum stable checkpoint at any non-faulty replica

  Request *rr; // Outstanding recovery request or null if
  // there is no outstanding recovery request.
  Certificate<Reply> rr_reps; // Certificate with replies to recovery request.
  View *rr_views; // Views in recovery replies.

  Seqno recovery_point; // Seqno_max if not known

  //
  // Pointers to various functions.
  //


  void (*non_det_choices)(Seqno, Byz_buffer *);
  int max_nondet_choice_len;

  bool print_stuff;

#ifdef FAIRNESS_ADAPTIVE
  Seqno send_view_change_now;
  int max_rqueue_size; // maximum size of rqueue during a checkpoint interval
  Seqno *executed_snapshot; //arrays of Seqnos used to check fairness
  //when a client add a request to rqueue, executed_snapshot[client_id] = last_executed
  //when the request is removed from the queue (in execute_committed) the fairness
  //is violated if the request seqno is higher than executed_snapshot[client_id]+fairness_bound
  int fairness_bound; // 2 * num_principals should be a good higher bound...
#endif

  bool vc_already_triggered; // true if one of the adaptive algoriths have already triggered a view change
  unsigned long req_count; // the count of requests executed since the
  // last checkpoint
};

// Pointer to global replica object.
extern Verifier *replica;

extern "C" void *start_verifier_thread(void *);
extern "C" void *start_execution_thread(void *);

inline int Verifier::max_nd_bytes() const
{
  return max_nondet_choice_len;
}

inline int Verifier::used_state_bytes() const
{
  return replies.size();
}

#ifdef FAIRNESS_ADAPTIVE
inline unsigned long Verifier::get_req_count()
{
  return req_count;
}

inline void Verifier::increment_req_count()
{
  req_count++;
  return;
}
#endif

inline void Verifier::modify(char *mem, int size)
{
  state.cow(mem, size);
}

inline void Verifier::modify_index(int bindex)
{
  state.cow_single(bindex);
}

inline bool Verifier::has_new_view() const
{
  return v == 0 || (has_nv_state && vi.has_new_view(v));
}

template<class T> inline void Verifier::gen_handle(Message *m)
{
  T *n;
  if (T::convert(m, n))
  {
    printf("I have converted a request properly\n");
    handle(n);
  }
  else
  {
    printf("The conversion has failed\n");
    delete m;
  }
}

inline bool Verifier::delay_vc()
{
  return state.in_fetch_state();
}

inline Big_req_table* Verifier::big_reqs()
{
  return &brt;
}

#endif //_Replica_h
