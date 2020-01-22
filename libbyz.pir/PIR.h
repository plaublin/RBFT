#ifndef _Replica_h
#define _Replica_h 1

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
#include "z2z_async_dlist.h"

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
class Node_blacklister;
class Req_list;

extern void delay_pre_prepare_timer_handler();

void print_statistics_before_dying(int sig);

#ifdef DELAY_ADAPTIVE
extern void pre_prepare_timer_handler();
//extern void call_pre_prepare_timer_handler();
#endif

#ifdef THROUGHPUT_ADAPTIVE
extern void throughput_timer_handler();
extern void increment_timer_handler();
#endif

extern void vtimer_handler();
extern void stimer_handler();
extern void ntimer_handler();

#ifdef DEBUG_PERIODICALLY_PRINT_THR
extern void pipes_thr_handler();
#endif

#define ALIGNMENT_BYTES 2

struct pre_prepare_in_list
{
  Pre_prepare *pp;
  struct list_head link;
};

class PIR: public Node
{
public:

  PIR(FILE *config_file, FILE *config_priv, char *mem, int nbytes,
      int byz_pre_prepare_delay = 0, bool small_batches = false,
      long int exec_command_delay = 0);
  // Requires: "mem" is vm page aligned and nbytes is a multiple of the
  // vm page size.
  // Effects: Create a new server replica using the information in
  // "config_file" and "config_priv". The replica's state is set to the
  // "nbytes" of memory starting at "mem".

  virtual ~PIR();
  // Effects: Kill server replica and deallocate associated storage.

  // Init the connection with the replicas
  void init_comm_with_replicas(void);

  // Init the connection with the verifier
  void init_comm_with_verifier(void);

  void recv();
  // Effects: Loops receiving messages. if idle, may dequeue status and fetch messages and dispatch
  // them to the appropriate handlers

  void read_all_remaining_messages(void);

  void jump_table(Message *m);
  //dispatches m to the appropriate handler (or queue)

  void send(Message *m, int i);
  // used to send a message using the appropriate socket

  void send_noblock(Message *m, int i);
  // used to send a message using the appropriate socket
  // non-blocking

  void sendUDP(Message *m, int i);
  // send a message to node i using UDP

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

#ifdef FAIRNESS_ADAPTIVE
  // for each client, seqno of the last PP for which there have been a request
  Seqno *executed_seqno;
#endif

  float average_batch_size;
  int sum_batch_size;
  int nb_batch_sent;

  struct list_head pending_pp; // list of pending PP

  // Sets and logs to keep track of messages received. Their size
  // is equal to max_out.
  Req_queue rqueue; // For read-write requests.
  Req_queue ro_rqueue; // For read-only requests

  Req_list *rlist; // list of requests that cannot be added to the rqueue yet

#ifdef DEBUG_PERIODICALLY_PRINT_THR
  // Timer for periodically printing the throughput of the different pipes
  ITimer *pipes_thr_timer;
  Time pipes_thr_start;
  int nb_received_requests;
  int nb_sent_ordered_requests;
  int *nb_sent_to_replicas;
  int *nb_recv_from_replicas;
#endif

private:
  friend class State;
  friend class Wrapped_request;
  //
  // Message handlers:
  //
  void handle(Request* m);
  void handle(Pre_prepare* m, bool force_send = false);
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
  void handle(Wrapped_request *m);
  void handle_request(Request *req);
  void handle(Protocol_instance_change* m);
  // Effects: Execute the protocol steps associated with the arrival
  // of the argument message.

  // verify a request coming from a Client
  Request* verify(Wrapped_request* m);

  // to mimics the execute_committed() -> execute_prepared() relation
  void truly_send_batch_to_verifier();

  // send a batch to the Verifier
  void send_batch_to_verifier();

  // return true if the PP with seqno s is already pending, false otherwise
  bool pp_already_pending(Seqno s);

  // return true if the replica has locally received all
  // the requests in the PP; false otherwise
  bool got_all_requests_of_pp(Pre_prepare *pp);

  void delete_all_requests_of_pp(Pre_prepare *pp, const bool upto);
  void send_wrapped_request_and_delete_requests_of_pp(Pre_prepare *pp, View v, Seqno s, const char *fname, const bool upto);
    
  friend void delay_pre_prepare_timer_handler();

#ifdef DELAY_ADAPTIVE
  friend void pre_prepare_timer_handler();
#endif

#ifdef THROUGHPUT_ADAPTIVE
  friend void throughput_timer_handler();
  friend void increment_timer_handler();
#endif

  friend void vtimer_handler();
  friend void stimer_handler();
  friend void ntimer_handler();
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

  void send_new_key();
  // Effects: Calls Node's send_new_key, adjusts timer and cleans up
  // stale messages.

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


  int *snd_socks;
  int *rcv_socks;
  int bootstrap_socket;
  struct sockaddr_in bootstrap_sin;

  /* for connection to the verifier */
  struct sockaddr_in verifier_sin;
  int verifier_socket_fd;

  // kzimp file descriptors
  int verifier_to_pir_fd;
  int pir_to_verifier_fd;

  Node_blacklister* replica_blacklister;

  int can_send_to_node(int i) const;

  // Logging variables used to measure average batch size
  int nbreqs; // The number of requests executed in current interval
  int nbrounds; // The number of rounds of BFT executed in current interval

  Seqno last_stable; // Sequence number of last stable state.
  Seqno low_bound; // Low bound on request sequence numbers that may
  // be accepted in current view.

  Seqno last_prepared; // Sequence number of highest prepared request
  Seqno last_executed; // Sequence number of last executed message.
  Seqno last_tentative_execute; // Sequence number of last message tentatively
  // executed.

  long int exec_command_delay; //delay caused by a request execution.

  Log<Prepared_cert> plog;

  Big_req_table brt; // Table with big requests
  friend class Big_req_table;

  Log<Certificate<Commit> > clog;
  Log<Certificate<Checkpoint> > elog;

  // Set of stable checkpoint messages above my window.
  Set<Checkpoint> sset;

  // Last replies sent to each principal.
  Rep_info replies;

  // State abstraction manages state checkpointing and digesting
  State state;

  ITimer *stimer; // Timer to send status messages periodically.
  Time last_status; // Time when last status message was sent

  void compute_requests_throughput(bool send);

  Time send_ordered_req_start;
  Time recv_req_start;
  int nb_sent_ordered_req;
  int nb_recv_req;

  //
  // View changes:
  //
  View_info vi; // View-info abstraction manages information about view changes
  ITimer *vtimer; // View change timer

  bool need_requests; // flag if we need to gather client requests

#ifdef DELAY_ADAPTIVE
  ITimer *pre_prepare_timer;
#endif

#ifdef THROUGHPUT_ADAPTIVE
  ITimer *throughput_timer; //Very short view change timer triggered by violations of the expected throughput (only in saturation periods)
  ITimer *increment_timer;
#endif

  // the following timer is there to delay sending of pre_prepares
  ITimer *delay_pre_prepare_timer;

  struct delayed_pp
  {
    Pre_prepare *pp;
    Time t; // time at which the PP has been created, in cycles
    struct list_head link;
  };

  struct list_head delayed_pps;


  bool limbo; // True iff moved to new view but did not start vtimer yet.
  bool has_nv_state; // True iff replica's last_stable is sufficient
  // to start processing requests in new view.
  bool status_messages_badly_needed;
  Seqno not_deprioritize_status_before_this;

#ifdef REPLICA_FLOOD_PROTECTION
  bool flood_protection_active;
  Seqno flood_protection_view;
  int rmcount[5];
#endif

  //
  // Recovery
  //
  bool rec_ready; // True iff replica is ready to recover
  bool recovering; // True iff replica is recovering.
  bool vc_recovering; // True iff replica exited limbo for a view after it started recovery
  bool corrupt; // True iff replica's data was found to be corrupt.

  ITimer* ntimer; // Timer to trigger transmission of null requests when system is idle

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
  Seqno max_rec_n; // Maximum sequence number of a recovery request in state.

  //
  // Pointers to various functions.
  //
  int
  (*exec_command)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool, long int);

  void (*non_det_choices)(Seqno, Byz_buffer *);
  int max_nondet_choice_len;

  bool print_stuff;

#ifdef THROUGHPUT_ADAPTIVE
#ifdef THROUGHPUT_ADAPTIVE_LAST_VIEW
  Time last_view_time; // time the last checkpoint completed
#else
  Time last_cp_time; // time the last checkpoint completed
#endif
  float *last_throughput_of_replica; // last throughput observed for each primary
  float req_throughput; // the required throughput for the system
  float req_throughput_increment;
  bool time_to_increment;
  int checkpoints_in_new_view;
  bool saturated; // true if the system has remained
  // saturated since the last checkpoing,
  // false otherwise.
  bool first_checkpoint_after_view_change; // true if we are in the first checkpoint interval after a view change happened
  float highest_throughput_ever;
#endif

  bool vc_already_triggered; // true if one of the adaptive algoriths have already triggered a view change
  unsigned long req_count; // the count of requests executed since the
  // last checkpoint
#ifdef THROUGHPUT_ADAPTIVE_LAST_VIEW
  unsigned long req_count_vc; // the count of requests executed since the
  // last view change
#endif

#ifdef DELAY_ADAPTIVE
  int received_pre_prepares; //pre_prepares received since the last check
#endif

  bool* excluded_replicas; //array of bool. if excluded_replicas[i] == true, replica i is not added to the socket i listen in select
  bool excluded_clients;

  int client_socket; //read client request from there
  // int client_port;
  //int interval;
  // char* message;
  bool* blacklisted; //this will point to an array of num_principals bools, initialized at 0
  //client with id client_id is blacklisted iff blacklisted[client_id]==true
  Addr client_address;

};


// Pointer to global replica object.
extern PIR *replica;

extern "C" void *start_verifier_thread(void *);

inline int PIR::max_nd_bytes() const
{
  return max_nondet_choice_len;
}

inline int PIR::used_state_bytes() const
{
  return replies.size();
}

inline void PIR::modify(char *mem, int size)
{
  state.cow(mem, size);
}

inline void PIR::modify_index(int bindex)
{
  state.cow_single(bindex);
}

inline bool PIR::has_new_view() const
{
  return v == 0 || (has_nv_state && vi.has_new_view(v));
}

template<class T> inline void PIR::gen_handle(Message *m)
{
  T *n;
  if (T::convert(m, n))
  {
    handle(n);
  }
  else
  {
    delete m;
  }
}

inline bool PIR::delay_vc()
{
  return state.in_fetch_state();
}

inline Big_req_table* PIR::big_reqs()
{
  return &brt;
}

inline int PIR::can_send_to_node(int i) const {
#if defined(ATTACK1)
  return ( replica->protocol_instance_id !=0 || (replica->protocol_instance_id == 0 && (send_mask & 1<<i)) );
#elif defined(ATTACK2)
  return ( (replica->protocol_instance_id == 0 && (send_mask & 1<<i))
          || (replica->protocol_instance_id == 1 && (0x7 & 1<<i))
          || (replica->protocol_instance_id == 2 && (0xB & 1<<i)) );
#else
  return (send_mask & 1<<i);
  //PL: flooding node
  //return ( (replica->protocol_instance_id == 0 && (0x7 & 1<<i))
  //        || (replica->protocol_instance_id == 1 && (0xB & 1<<i))
  //        || (replica->protocol_instance_id == 2 && (0xD & 1<<i)) );
#endif
}

#endif //_Replica_h
