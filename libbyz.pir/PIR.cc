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
#include <netinet/tcp.h> // for TCP_NODELAY
#include <signal.h>

#include "Array.h"
#include "Array.t"
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
#include "PIR.h"
#include "Statistics.h"
#include "State_defs.h"
#include "attacks.h"
#include "parameters.h"
#include "tcp_net.h"
#include "Protocol_instance_change.h"
#include "Node_blacklister.h"
#include "Req_list.h"

// Global replica object.
PIR *replica;

// Force template instantiation
#include "Certificate.t"
template class Certificate<Commit> ;
template class Certificate<Checkpoint> ;
template class Certificate<Reply> ;

#include "Log.t"
template class Log<Prepared_cert> ;
template class Log<Certificate<Commit> > ;
template class Log<Certificate<Checkpoint> > ;

#include "Set.t"
template class Set<Checkpoint> ;

template<class T>
void PIR::retransmit(T *m, Time &cur, Time *tsent, Principal *p)
{
  // re-authenticate.
  m->re_authenticate(p);
  //    fprintf(stderr, "RET: %s to %d \n", m->stag(), p->pid());
  // Retransmit message
#ifdef MSG_DEBUG
  //fprintf(stderr, "Replica is retransmitting to %i\n", p->pid());
#endif

  send(m, p->pid());
}

void PIR::init_comm_with_replicas(void)
{
#if USE_TCP_CONNECTIONS
  rcv_socks = (int*) malloc(sizeof(int) * num_replicas);
  snd_socks = (int*) malloc(sizeof(int) * num_replicas);
  for (int i = 0; i < num_replicas; i++)
  {
    rcv_socks[i] = -1;
    snd_socks[i] = -1;
  }
  bootstrap_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (bootstrap_socket == -1)
  {
    perror("Error while creating the socket, exiting! ");
    exit(errno);
  }

  int flag;
  int result;

  // TCP NO DELAY
  flag = 1;
  result = setsockopt(bootstrap_socket, IPPROTO_TCP, TCP_NODELAY,
      (char*) &flag, sizeof(int));
  if (result == -1)
  {
    perror("Error while setting TCP NO DELAY! ");
  }

  // 2) bind on it
  bootstrap_sin.sin_addr.s_addr = htonl(INADDR_ANY);
  bootstrap_sin.sin_family = AF_INET;
  bootstrap_sin.sin_port = htons(PIR_BOOTSTRAP_PORT + id()); // we add id so that it is possible to launch the replicas on the same machine

  if (bind(bootstrap_socket, (struct sockaddr*) &bootstrap_sin,
      sizeof(bootstrap_sin)) == -1)
  {
    perror("Error while binding to the socket! ");
    exit(errno);
  }

  // 3) make the socket listening for incoming connections
  if (listen(bootstrap_socket, num_replicas + 1) == -1)
  {
    perror("Error while calling listen! ");
    exit(errno);
  }

  // 4) connect to the other replicas
  for (int i = 0; i <= id(); i++)
  {
    // 1) create the socket
    snd_socks[i] = socket(AF_INET, SOCK_STREAM, 0);
    if (snd_socks[i] == -1)
    {
      perror("Error while creating the socket! ");
      exit(errno);
    }

    // 2)  connect to the server
    // since we have multiple NICs, use replicas_ipaddr[], and not replicas_hostname[]
    unsigned int s_addr;
    struct sockaddr_in addr;

    s_addr = inet_addr(replicas_ipaddr[i]);
    if (f() == 1) {
        NIPQUADi(s_addr, 2) = get_interface_between_i_and_j(protocol_instance_id,
                id(), i);
    }
    addr.sin_addr.s_addr = s_addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PIR_BOOTSTRAP_PORT + i); // we add i so that it is possible to launch the replicas on the same machine

    fprintf(stderr,
        "PIR %d connects to PIR %d at %3d.%3d.%3d.%3d:%i (instance %d)\n",
        id(), i, NIPQUAD(s_addr),ntohs(addr.sin_port), protocol_instance_id) ;

    // 2b) TCP NO DELAY
    flag = 1;
    result = setsockopt(snd_socks[i], IPPROTO_TCP, TCP_NODELAY, (char *) &flag,
        sizeof(int));

    if (result == -1)
    {
      perror("Error while setting TCP NO DELAY! ");
      exit(-1);
    }

    // 2c) connect
    while (true)
    {
      if (connect(snd_socks[i], (struct sockaddr *) &(addr), sizeof(addr)) < 0)
      {
        perror("Cannot connect, attempting again..");
        sleep(1);
      }
      else
      {
        fprintf(stderr, "Connection successful from %i to %i!\n", id(), i);
        break;
      }
    }
  }

  // 5) accepting connections from my id to the last replica
  for (int i = 0; i < num_replicas - id(); i++)
  {
    struct sockaddr_in csin;
    int sinsize = sizeof(csin);
    int rcv_sock = accept(bootstrap_socket, (struct sockaddr*) &csin,
        (socklen_t*) &sinsize);

    if (rcv_sock == -1)
    {
      perror("An invalid socket has been accepted! ");
      continue;
    }

    // TCP NO DELAY
    flag = 1;
    result = setsockopt(rcv_sock, IPPROTO_TCP, TCP_NODELAY, (char *) &flag,
        sizeof(int));
    if (result == -1)
    {
      perror("Error while setting TCP NO DELAY! ");
    }

    unsigned int s_addr;
    //char *hostname = inet_ntoa(csin.sin_addr);
    int id_of_rcv = 0;
    while (id_of_rcv < num_replicas)
    {
      s_addr = inet_addr(replicas_ipaddr[id_of_rcv]);
      if (f() == 1) {
          NIPQUADi(s_addr, 2) = get_interface_between_i_and_j(protocol_instance_id,
                  id(), id_of_rcv);
      }
      if (csin.sin_addr.s_addr == s_addr)
      {
        break;
      }
      id_of_rcv++;
    }

    if (id_of_rcv >= num_replicas)
    {
      fprintf(stderr, "Unknown host: %3d.%3d.%3d.%3d\n",
          NIPQUAD(csin.sin_addr.s_addr));
    }

    rcv_socks[id_of_rcv] = rcv_sock;

    // print some information about the accepted connection
    fprintf(stderr,
        "A connection has been accepted from %3d.%3d.%3d.%3d:%i, id=%i\n",
        NIPQUAD(s_addr),ntohs(csin.sin_port), id_of_rcv) ;

  }

  for (int i = 0; i < num_replicas; i++)
  {
    if (rcv_socks[i] == -1)
    {
      rcv_socks[i] = snd_socks[i];
    }
    if (snd_socks[i] == -1)
    {
      snd_socks[i] = rcv_socks[i];
    }
  }

  fprintf(stderr, "List of sockets:\n");
  for (int i = 0; i < num_replicas; i++)
  {
    struct sockaddr_in peer_addr;
    socklen_t peer_addr_len;
    getpeername(rcv_socks[i], (struct sockaddr*)&peer_addr, &peer_addr_len);
    fprintf(stderr, "%d\t%d\t%3d.%3d.%3d.%3d:%d\n", i, rcv_socks[i], NIPQUAD(peer_addr.sin_addr.s_addr), ntohs(peer_addr.sin_port));
  }
  fprintf(stderr, "===================\n");

#else

  snd_socks = NULL;
  rcv_socks = (int*) malloc(sizeof(int) * num_replicas);

  // for each replica i
  //   create socket on port PIR_BOOTSTRAP_PORT + protocol_instance_id + i
  //   bind on it (it will be for comm from replica i)
  for (int i = 0; i < num_replicas; i++)
  {
    // initializing
    rcv_socks[i] = socket(AF_INET, SOCK_DGRAM, 0);

    Addr tmp;
    tmp.sin_family = AF_INET;
    tmp.sin_port = htons(PIR_BOOTSTRAP_PORT + protocol_instance_id*10 + i);
    tmp.sin_addr.s_addr = inet_addr(replicas_ipaddr[id()]);

    if (f() > 1) {
      in_addr_t s_addr = principals[i]->address()->sin_addr.s_addr;
      NIPQUADi(tmp.sin_addr.s_addr, 2) = NIPQUADi(s_addr, 2);
    }

    fprintf(stderr,
        "Replica %d_%d binds on %d.%d.%d.%d:%i for comm with replica %d\n",
        protocol_instance_id, id(), NIPQUAD(tmp.sin_addr.s_addr), ntohs(tmp.sin_port), i);

    //binding
    int error = bind(rcv_socks[i], (struct sockaddr*) &tmp, sizeof(Addr));
    if (error < 0)
    {
      perror("Unable to name socket");
      exit(1);
    }

    //making the socket async
    error = fcntl(rcv_socks[i], F_SETFL, O_NONBLOCK);
    if (error < 0)
    {
      perror("unable to set socket to asynchronous mode");
      exit(1);
    }

    //for the send address, modify principals[i]
    principals[i]->updateAddrPort(PIR_BOOTSTRAP_PORT + protocol_instance_id*10 + id());

    /*
    const Addr *to = principals[i]->address();
    fprintf(stderr,
        "Replica %d_%d can send a message to node %d via %d.%d.%d.%d:%i\n",
        protocol_instance_id, id(), i,
        NIPQUAD(to->sin_addr.s_addr),ntohs(to->sin_port));
     */
  }

#endif
}

void PIR::init_comm_with_verifier(void)
{
  /*********************************************************************/
  /************* kZIMP for Verifier <-> PIRs communication *************/
  /*********************************************************************/

  char chaname[256];

  snprintf(chaname, 256, "%s%i", KZIMP_CHAR_DEV_FILE, protocol_instance_id);
  pir_to_verifier_fd = open(chaname, O_WRONLY);
  fprintf(stderr, "pir_to_verifier_fd = (%i, %s)\n", pir_to_verifier_fd,
      chaname);

  // define FLOODING_BUT_NOT_FOR_FORWARDER if the forwarder does not flood
  // otherwise kzimp channel becomes full and the protocol stops its execution.
#ifdef FLOODING_BUT_NOT_FOR_FORWARDER

#if defined(ATTACK1)
  if (! (floodMax() && protocol_instance_id == FAULTY_PROTOCOL_INSTANCE) )
  {

#elif defined(ATTACK2)
    if (!(floodMax() && protocol_instance_id != FAULTY_PROTOCOL_INSTANCE)
        || (floodMax() && protocol_instance_id == FAULTY_PROTOCOL_INSTANCE && id() == 0) )
    {

#else
      if (!floodMax())
      {
#endif

#endif

  snprintf(chaname, 256, "%s%i", KZIMP_CHAR_DEV_FILE, num_pirs());
  verifier_to_pir_fd = open(chaname, O_RDONLY);
  fprintf(stderr, "verifier_to_pir_fd = (%i, %s)\n", verifier_to_pir_fd,
      chaname);

#ifdef FLOODING_BUT_NOT_FOR_FORWARDER
}
#endif
}

PIR::PIR(FILE *config_file, FILE *config_priv, char *mem, int nbytes,
    int _byz_pre_prepare_delay, bool _small_batches,
    long int _exec_command_delay) :
  Node(config_file, config_priv), rqueue(), ro_rqueue(), plog(max_out),
      clog(max_out), elog(max_out * 2, 0), sset(n()),
      replies(mem, nbytes, num_principals), state(this, mem, nbytes),
      vi(node_id, 0)
{
  // Fail if node is not a replica.
  if (!is_replica(id()))
    th_fail("Node is not a replica");

  // The following two lines implement the 'slow' primary
  byz_pre_prepare_delay = _byz_pre_prepare_delay;
  small_batches = _small_batches;
  exec_command_delay = _exec_command_delay;

#ifdef THROUGHPUT_ADAPTIVE

  last_view_time = 0;
  last_cp_time = 0;

  req_throughput = req_throughput_init;
  req_throughput_increment = req_throughput_increment_init;
  first_checkpoint_after_view_change = false;
  highest_throughput_ever = 0;
  vc_already_triggered = false;
  time_to_increment = false;
  checkpoints_in_new_view = 0;

  last_throughput_of_replica = (float*) malloc(num_replicas * sizeof(float));

  for (int j = 0; j < num_replicas; j++)
  last_throughput_of_replica[j] = req_throughput_init;
#endif

  seqno = 0;
  last_stable = 0;
  low_bound = 0;

  last_prepared = 0;
  last_executed = 0;
  last_tentative_execute = 0;
#ifdef USE_GETTIMEOFDAY
  last_status.tv_sec=0;
  last_status.tv_nsec=0;
#else
  last_status = 0;
#endif

  nb_batch_sent = 0;
  average_batch_size = 0.0;
  sum_batch_size = 0;
  limbo = false;
  has_nv_state = true;

  status_messages_badly_needed = false;
  not_deprioritize_status_before_this = 0;

  nbreqs = 0;
  nbrounds = 0;

  // Read view change, status, and recovery timeouts from replica's portion
  // of "config_file"
  int vt, st, rt;
  fscanf(config_file, "%d\n", &vt);
  fscanf(config_file, "%d\n", &st);
  fscanf(config_file, "%d\n", &rt);

  //PL: this is better to close the file descriptor when it is no longer used
  fclose(config_file);

  // Create timers and randomize times to avoid collisions.
  srand48( getpid());

  signal(SIGINT, print_statistics_before_dying);
  signal(SIGPIPE, print_statistics_before_dying);

  delay_pre_prepare_timer = new ITimer(byz_pre_prepare_delay,
      delay_pre_prepare_timer_handler);

  INIT_LIST_HEAD(&delayed_pps);

#ifdef DELAY_ADAPTIVE
  //  call_pre_prepare_timer = new ITimer(call_pre_prepare_timer_duration, call_pre_prepare_timer_handler);
  pre_prepare_timer = new ITimer(pre_prepare_timer_duration,
      pre_prepare_timer_handler);
  pre_prepare_timer->start();
#endif

#ifdef THROUGHPUT_ADAPTIVE
  throughput_timer = new ITimer(throughput_timer_duration,
      throughput_timer_handler);
  increment_timer = new ITimer(increment_timer_duration,
      increment_timer_handler);
#endif

  vtimer = new ITimer(vt + lrand48() % 100, vtimer_handler);
  stimer = new ITimer(st + lrand48() % 100, stimer_handler);

  // Skew recoveries. It is important for nodes to recover in the reverse order
  // of their node ids to avoid a view-change every recovery which would degrade
  // performance.
  rec_ready = false;
  ntimer = new ITimer(30000 / max_out, ntimer_handler);

  recovering = false;
  rr = 0;
  rr_views = new View[num_replicas];
  recovery_point = Seqno_max;
  max_rec_n = 0;

  exec_command = 0;
  non_det_choices = 0;

  print_stuff = true;

  INIT_LIST_HEAD(&pending_pp);

  rlist = new Req_list(num_clients(), num_replicas);

#ifdef FAIRNESS_ADAPTIVE
  executed_seqno = new Seqno[num_clients()];
  for (int i=0; i<num_clients(); i++) {
    executed_seqno[i]=0;
  }
#endif

#ifdef DEBUG_PERIODICALLY_PRINT_THR
  pipes_thr_timer = new ITimer(DEBUG_PERIODIC_THR_PERIOD, pipes_thr_handler);
  pipes_thr_start = currentTime();
  nb_received_requests = 0;
  nb_sent_ordered_requests = 0;
  nb_sent_to_replicas = new int[n()];
  for(int i=0; i<n(); i++) nb_sent_to_replicas[i] = 0;
  nb_recv_from_replicas = new int[n()];
  for(int i=0; i<n(); i++) nb_recv_from_replicas[i] = 0;
#endif

  init_comm_with_verifier();

  excluded_clients = false;

  /*********************************************************************/

  excluded_replicas = (bool*) malloc(num_replicas * sizeof(bool));

  fprintf(stderr, "I am replica %d. Am I the primary? %s\n", id(),
      (id() == primary() ? "yes" : "no"));

  for (int j = 0; j < num_replicas; j++)
  {
    //resetting the excluded_replicas array
    excluded_replicas[j] = false;
  }

  status_pending = (Message**) malloc(num_replicas * sizeof(Message*));
  for (int index = 0; index < num_replicas; index++)
  {
    status_pending[index] = NULL;
  }
  status_to_process = 0;
  s_identity = -1;

  //blacklist malloc and initialization
  blacklisted = (bool*) malloc(sizeof(bool) * num_principals);
  for (int i = 0; i < num_principals; i++)
  {
    blacklisted[i] = false;
  }

  init_comm_with_replicas();

  fprintf(stderr,
      "********* PIR of protocol instance %i launched **********\n",
      protocol_instance_id);
}

void PIR::register_exec(
    int(*e)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool, long int))
{ //last long int is the execute delay
  exec_command = e;
}

void PIR::register_nondet_choices(void(*n)(Seqno, Byz_buffer *), int max_len)
{

  non_det_choices = n;
  max_nondet_choice_len = max_len;
}

void PIR::compute_non_det(Seqno s, char *b, int *b_len)
{
  if (non_det_choices == 0)
  {
    *b_len = 0;
    return;
  }
  Byz_buffer buf;
  buf.contents = b;
  buf.size = *b_len;
  non_det_choices(s, &buf);
  *b_len = buf.size;
}

PIR::~PIR()
{
}

void PIR::recv()
{
  Message *mp;

  fd_set file_descriptors; //set of file descriptors to listen to (only one socket, in this case)
  timeval listen_time; //max time to wait for something readable in the file descriptors
  // non-blocking socket listening
  // (return earlier if something becames readable)

  // Compute session keys and send initial new-key message.
  Node::send_new_key();

  // Compute digest of initial state and first checkpoint.
  state.compute_full_digest();

  // Start status and authentication freshness timers
  stimer->start();
  atimer->start();
  if (id() == primary())
    ntimer->start();

#ifdef DEBUG_PERIODICALLY_PRINT_THR
  pipes_thr_timer->start();
#endif

  // Allow recoveries
  rec_ready = true;

  fprintf(stderr, "Replica ready\n");
  
  // print to which nodes I can send my messages
  fprintf(stderr, "I can send to the nodes:");
  for (int j = 0; j < num_replicas; j++) {
    fprintf(stderr, " %d: %s", j, (can_send_to_node(j) ? "true" : "false"));
  }
  fprintf(stderr, "\n");

  if (floodMax()
#if defined(ATTACK1)
  && protocol_instance_id == FAULTY_PROTOCOL_INSTANCE
#elif defined(ATTACK2)
  && protocol_instance_id != FAULTY_PROTOCOL_INSTANCE
#endif
  )
  {
    fprintf(stderr, "I am Byzantine and will flood to the max.\n");

    while (1)
    {
      FD_ZERO(&file_descriptors); //initialize file descriptor set

      int maxsock;
#ifdef FLOODING_BUT_NOT_FOR_FORWARDER
      maxsock = 0;
#else
      FD_SET(verifier_to_pir_fd, &file_descriptors);
      maxsock = verifier_to_pir_fd;
#endif

      for (int j = 0; j < num_replicas; j++)
      {
        FD_SET(rcv_socks[j], &file_descriptors);
        maxsock = MAX(maxsock, rcv_socks[j]);
      }

      listen_time.tv_sec = 0;
      listen_time.tv_usec = 500;

      select(maxsock + 1, &file_descriptors, NULL, NULL, &listen_time);

      // FLOODING_BUT_NOT_FOR_FORWARDER has to be defined
      // when the communication between the dispatch&monitoring thread
      // and the PIR is deactivated
#ifndef FLOODING_BUT_NOT_FOR_FORWARDER
      if (FD_ISSET(verifier_to_pir_fd, &file_descriptors))
      {
        Message *m = new Message(Max_message_size);

        int ret = read(verifier_to_pir_fd, m->contents(), m->msize());

#ifdef DEBUG_PERIODICALLY_PRINT_THR
          __sync_fetch_and_add (&nb_received_requests, 1);
#endif

#ifdef FLOODER_TAKES_PART_IN_PROTOCOL
        if (ret >= (int) sizeof(Message_rep) && ret >= m->size())
        {
          if (m->tag() == Wrapped_request_tag)
          {
            Wrapped_request *wr = (Wrapped_request*) m;
            handle(wr);
          }
          else if (m->tag() == Request_tag)
          {
            handle_request((Request*) m);
          }
          else if (m->tag() == Protocol_instance_change_tag)
          {
            handle((Protocol_instance_change*) m);
            delete m;
          }
          else
          {
            fprintf(stderr,
                "PIR %i_%i has received a message whose tag is %i\n",
                protocol_instance_id, id(), m->tag());
            delete m;
          }
        }
        else
        {
          delete m;
          // fprintf(stderr, "--------- WTF? --------- ret: %d from replica %d\n",ret,j);
          if (ret < 0)
            fprintf(stderr, "errno: %s\n", strerror(errno));
        }
#else
        delete m;
#endif
      }
#endif

      for (int j = 0; j < num_replicas; j++)
      {
        // for each replica
        if (FD_ISSET(rcv_socks[j], &file_descriptors))
        {
          mp = new Message(Max_message_size);

#ifdef DEBUG_PERIODICALLY_PRINT_THR
          __sync_fetch_and_add (&nb_recv_from_replicas[j], 1);
#endif

#if USE_TCP_CONNECTIONS
          // 1) first of all, we need to receive the Message_rep (in order to get the message size)
          int msg_rep_size = recvMsg(rcv_socks[j], (void*) mp->contents(),
              sizeof(Message_rep));

          // 2) now that we have the size of the message, receive the content
          int msg_size = recvMsg(rcv_socks[j],
              (void*) ((char*) mp->contents() + msg_rep_size),
              mp->size() - msg_rep_size);

          int ret = msg_rep_size + msg_size;
#else
          int ret = recvfrom(rcv_socks[j], mp->contents(), mp->msize(), 0, 0, 0);
#endif

#ifdef FLOODER_TAKES_PART_IN_PROTOCOL
          //take part in the protocol
          if (ret >= (int) sizeof(Message_rep) && ret >= mp->size())
          {
            if (mp->tag() == Request_tag)
            {
              delete mp;
            }
            else
            {
              jump_table(mp);
            }
          }
          else
          {
            delete mp;
            // fprintf(stderr, "--------- WTF? --------- ret: %d from replica %d\n",ret,j);
            if (ret < 0)
                fprintf(stderr, "errno: %s\n", strerror(errno));
          }

#else
          //Do not take part in the protocol
          delete mp;
#endif
        }
      }

      //send an unvalid Message of max size to all nodes
      mp = new Message(Request_tag, Max_message_size);
      ((Request_rep*) (mp->contents()))->cid = num_replicas;
      ((Request_rep*) (mp->contents()))->command_size = Max_message_size
          - sizeof(Request_rep) - node->auth_size(num_replicas);
      Message::set_mac_unvalid((Message_rep*) mp->contents());
      //send(mp, All_replicas);
      delete mp;
    }
  }

  if (floodProtocol())
  {
    fprintf(stderr, "I am Byzantine and will flood with protocol messages.\n");
  }

  replica_blacklister = new Node_blacklister(num_replicas,
      NODE_BLACKLISTER_SLIDING_WINDOW_SIZE,
      READING_FROM_BLACKLISTED_NODES_PERIOD);

  int count_idle = 0;
  //int max_queue_size=0;
  //int silly_counter=1;

#ifdef REPLICA_FLOOD_PROTECTION

  int max_replica_messages;
  int max_replica_messages_index;
  int second_max_replica_messages;
  int second_max_replica_messages_index;

  for (int k = 0; k < 5; k++)
  rmcount[k] = 0;
  flood_protection_active = true;
  flood_protection_view = 0;
#endif

  Message* m;
  int nb_iterations = 0;
  while (1)
  {
    if (floodProtocol())
    {
      // flood with protocol messages (status and request)
      send_status(true);
      state.send_fetch(true);
      continue;
    }

    FD_ZERO(&file_descriptors); //initialize file descriptor set

    FD_SET(verifier_to_pir_fd, &file_descriptors);
    int maxsock = verifier_to_pir_fd;

    for (int j = 0; j < num_replicas; j++)
    {
      if (!excluded_replicas[j] && (!replica_blacklister->is_blacklisted(j)
          || replica_blacklister->time_to_read_from_blacklisted()))
      {
        FD_SET(rcv_socks[j], &file_descriptors);
        maxsock = MAX(maxsock, rcv_socks[j]);
      }
    }

    listen_time.tv_sec = 0;
    listen_time.tv_usec = 500;

    bool idle = true;
    select(maxsock + 1, &file_descriptors, NULL, NULL, &listen_time);

    ITimer::handle_timeouts();

    if (id() == primary() && protocol_instance_id == FAULTY_PROTOCOL_INSTANCE && id() == 0 && byz_pre_prepare_delay) {
      struct delayed_pp *dpp;
      struct list_head *pos, *n;
      list_for_each_safe(pos, n, &delayed_pps) {
        dpp = list_entry(pos, struct delayed_pp, link);

        unsigned long long now = rdtsc();

        if (diffTime(now, dpp->t)/1000.0 >= (float)byz_pre_prepare_delay) {
          //fprintf(stderr, "now= %qd ppt= %qd diff= %f pp_delay= %f\n", diffTime(now, 0), diffTime(dpp->t, 0), diffTime(now, dpp->t)/1000.0, (float)byz_pre_prepare_delay);

          send(dpp->pp, All_replicas);
          plog.fetch(seqno).add_mine(dpp->pp);
          
          list_del(pos);
          delete dpp;
        } else {
          break;
        }
      }
    }

    if (!excluded_clients)
    {
      /* get a new connection from the verifier */
      if (FD_ISSET(verifier_to_pir_fd, &file_descriptors))
      {
        m = new Message(Max_message_size);

        int ret = read(verifier_to_pir_fd, m->contents(), m->msize());

#ifdef DEBUG_PERIODICALLY_PRINT_THR
          __sync_fetch_and_add (&nb_received_requests, 1);
#endif

        if (ret >= (int) sizeof(Message_rep) && ret >= m->size())
        {
          if (m->tag() == Wrapped_request_tag)
          {
            Wrapped_request *wr = (Wrapped_request*) m;

            handle(wr);

            if (nb_iterations == 100000)
            {
              if (nb_batch_sent > 0)
                average_batch_size = sum_batch_size / nb_batch_sent;
              fprintf(stderr,
                  "Average batch size for %i batches sent is %f \n",
                  nb_batch_sent, average_batch_size);

            }
            nb_iterations++;
          }
          else if (m->tag() == Request_tag)
          {
            handle_request((Request*) m);
          }
          else if (m->tag() == Protocol_instance_change_tag)
          {
            handle((Protocol_instance_change*) m);
            delete m;
          }
          else
          {
            fprintf(stderr,
                "PIR %i_%i has received a message whose tag is %i\n",
                protocol_instance_id, id(), m->tag());
            delete m;
          }
        }
        else
        {
          delete m;
          // fprintf(stderr, "--------- WTF? --------- ret: %d from replica %d\n",ret,j);
          if (ret < 0)
            fprintf(stderr, "errno: %s\n", strerror(errno));
        }
      }
    }

    for (int j = 0; j < num_replicas; j++)
    {
      // for each replica
      if (!excluded_replicas[j])
      {
        //I want to listen at this replica
        if (FD_ISSET(rcv_socks[j], &file_descriptors))
        {
          idle = false;

#ifdef REPLICA_FLOOD_PROTECTION
          if (flood_protection_active)
          {
            /*
             if (j != primary())
             {
             */
            rmcount[j]++;
            rmcount[4]++;
            //}
            if (rmcount[4] > check_rmcount)
            {

              //time to check. compute max and second_max
              //fprintf(stderr, "Replica %i: %d %d %d %d %d\n", id(), rmcount[0], rmcount[1], rmcount[2], rmcount[3], rmcount[4]);
              max_replica_messages = -1;
              max_replica_messages_index = -1;
              second_max_replica_messages = -1;
              second_max_replica_messages_index = -1;

              // get the first max
              for (int k = 0; k < 4; k++)
              {
                /*
                 if (k != primary())
                 {
                 */
                //for all the non-primary replicas
                if (rmcount[k] > max_replica_messages)
                {
                  max_replica_messages = rmcount[k];
                  max_replica_messages_index = k;
                }
                //}
              }

              // get the second max
              for (int k = 0; k < 4; k++)
              {
                /*
                 if (k != primary())
                 {
                 */
                //for all the non-primary replicas
                if (rmcount[k] != max_replica_messages && rmcount[k]
                    > second_max_replica_messages)
                {
                  second_max_replica_messages = rmcount[k];
                  second_max_replica_messages_index = k;
                }
                //}
              }

              // if the values in the array are all the same ones, then set the second max to the first one
              // not to verify the assert
              if (second_max_replica_messages == -1)
              {
                second_max_replica_messages = max_replica_messages;
                second_max_replica_messages_index = max_replica_messages_index;
              }

              //fprintf(stderr, "max_replica_messages = %i, second_max_replica_messages = %i\n", max_replica_messages, second_max_replica_messages);
              th_assert((max_replica_messages != -1)
                  && (second_max_replica_messages != -1),
                  "Wrong computation of max_replica_message");

              // computed max and second_max. check triggering condition

              if (max_replica_messages >= second_max_replica_messages
                  * flood_detection_factor)
              {
                //FLOOD DETECTED! max_replica_messages_index is the flooder
                /*
                 fprintf(stderr,
                 " ********* FLOOD PROTECTION: excluding replica %d\n",
                 max_replica_messages_index);
                 replica_blacklister->blacklist_node(max_replica_messages_index);
                 */
              }

              for (int j = 0; j < 5; j++)
              rmcount[j] = 0;
            }
          }
#endif
          //fprintf(stderr, "Received something from replica %d\n",j);
          // something to read from replica j

          if (!excluded_replicas[j])
          {
#ifdef DEBUG_PERIODICALLY_PRINT_THR
          __sync_fetch_and_add (&nb_recv_from_replicas[j], 1);
#endif

            mp = new Message(Max_message_size);

#if USE_TCP_CONNECTIONS
            // 1) first of all, we need to receive the Message_rep (in order to get the message size)
            int msg_rep_size = recvMsg(rcv_socks[j], (void*) mp->contents(),
                sizeof(Message_rep));

            // 2) now that we have the size of the message, receive the content
            int msg_size = recvMsg(rcv_socks[j],
                (void*) ((char*) mp->contents() + msg_rep_size),
                mp->size() - msg_rep_size);

            int ret = msg_rep_size + msg_size;
#else
            int ret = recvfrom(rcv_socks[j], mp->contents(), mp->msize(), 0, 0, 0);
#endif

            if (ret >= (int) sizeof(Message_rep) && ret >= mp->size())
            {
              if (mp->tag() == Request_tag)
              {
                // requests coming from replicas are received only during
                // the replica flooding attack. Check the MAC and delete it.
                ((Request*) mp)->verify_without_digest();
                delete mp;

                replica_blacklister->add_message(j, false);
              }
              else
              {
                jump_table(mp);
              }
            }
            else
            {
              delete mp;
              // fprintf(stderr, "--------- WTF? --------- ret: %d from replica %d\n",ret,j);
              if (ret < 0)
                fprintf(stderr, "errno: %s\n", strerror(errno));
            }
          }
        }
      }
    }

    //checked clients and all the replicas, if idle is still true, I have nothing to do...
    if (idle)
      count_idle++;
    else
      count_idle = 0;
    if (count_idle > 3)
    {
      m = pick_next_status();
      if (m)
      {
        gen_handle<Status> (m);
      }
    }

#if 0
    //PL XXX flooding node hack
    /*attack2:*/ if (protocol_instance_id != FAULTY_PROTOCOL_INSTANCE && replica_blacklister->time_to_read_from_blacklisted()) {
    //if (replica_blacklister->time_to_read_from_blacklisted()) {
      Message *flooding_m = new Message(Request_tag, Max_message_size);
      ((Request_rep*) (flooding_m->contents()))->cid = num_replicas;
      ((Request_rep*) (flooding_m->contents()))->command_size = Max_message_size
          - sizeof(Request_rep) - node->auth_size(num_replicas);
      Message::set_mac_unvalid((Message_rep*) flooding_m->contents());
      ((Request*) flooding_m)->verify_without_digest();
  
      delete flooding_m;
    }
#endif

    replica_blacklister->new_reading_iteration();
  }
}

void PIR::sendUDP(Message *m, int i)
{
  const Addr *to = (i == All_replicas) ? group->address()
      : principals[i]->address();

  /*
   fprintf(stderr,
   "Replica %d_%d sends a message of tag %d to node %d: %d.%d.%d.%d:%i\n",
   protocol_instance_id, id(), m->tag(), i,
   NIPQUAD(to->sin_addr.s_addr), ntohs(to->sin_port));
   */

  int error = 0;
  int size = m->size();
  while (error < size)
  {
    error = sendto(sock, m->contents(), size, 0, (struct sockaddr*) to,
        sizeof(Addr));

#ifndef NDEBUG
    if (error < 0 && error != EAGAIN)
      perror("Node::send: sendto");
#endif
  }
}

void PIR::send(Message *m, int i)
{
  th_assert(i == All_replicas || (i >= 0 && i < num_principals),
      "Invalid argument");
  m->check_msg();

  if (i == All_replicas)
  {
    for (int x = 0; x < num_replicas; x++)
    {
      //  if (x != id()) //some code relies on receiving self-created messages
      replica->send(m, x);
    }
    return;
  }

  th_assert(i != All_replicas,
      "the multisocket version does not work well with multicast... \n");

  //printf("Sending msg tag to %i: %i\n", i, m->tag());
  if (i < num_replicas)
  {
    //XXX does not work if we replace can_send_to_node(i) by blacklister->is_blacklisted(i)
    //if (!replica_blacklister->is_blacklisted(i))
    if (can_send_to_node(i))
    {
#if USE_TCP_CONNECTIONS
      sendMsg(snd_socks[i], m->contents(), m->size());
#else
      sendUDP(m, i);
#endif

#ifdef DEBUG_PERIODICALLY_PRINT_THR
          __sync_fetch_and_add (&nb_sent_to_replicas[i], 1);
#endif
    }
  }
  else
  {
    // sendMsg(verifier_socket_fd, m->contents(), m->size());
    write(pir_to_verifier_fd, m->contents(), m->size());
  }
}

void PIR::send_noblock(Message *m, int i)
{
  th_assert(i == All_replicas || (i >= 0 && i < num_principals),
      "Invalid argument");
  m->check_msg();

  //TODO: if UDP, then call PIR::send()

  if (i == All_replicas)
  {
    for (int x = 0; x < num_replicas; x++)
    {
      //  if (x != id()) //some code relies on receiving self-created messages
      replica->send_noblock(m, x);
    }
    return;
  }

  th_assert(i != All_replicas,
      "the multisocket version does not work well with multicast... \n");

  //printf("Sending msg tag to %i: %i\n", i, m->tag());
  if (i < num_replicas)
  {
    sendMsg_noblock(snd_socks[i], m->contents(), m->size());
  }
  else
  {
    // sendMsg(verifier_socket_fd, m->contents(), m->size());
    write(pir_to_verifier_fd, m->contents(), m->size());
  }
}

// verify a request comming from a Client
Request* PIR::verify(Wrapped_request* wrapped)
{
  if (blacklisted[wrapped->client_id()])
  {
    /*
     fprintf(stderr, "[Replica %i] client %i is blacklisted\n",
     replica->id(), wrapped->client_id());
     */
    delete wrapped;
    return NULL;
  }

  bool verified_wrap = wrapped->verify_MAC();
  if (!verified_wrap)
  {
    /*
     fprintf(stderr, "[Replica %i] request %qd, mac from client %i is not valid\n",
     replica->id(), wrapped->seqno(), wrapped->client_id());
     */
    delete wrapped;
    return NULL;
  }

  // verify_MAC returned true, so digest and MAC are ok.
  // now we call verify_request to verify the signature.

  verified_wrap = wrapped->verify_request();
  if (!verified_wrap)
  {
    // adding the sender of this wrapped request to the blacklist
    //blacklisted[wrapped->client_id()] = true;
    fprintf(stderr,
        "*** +++ --- \\\\\\ Blacklisted client %d /// --- +++ ***\n",
        wrapped->client_id());
    delete wrapped;
    return NULL;
  }

  Wrapped_request::Requests_iter iter(wrapped);
  Request req;
  iter.get(req);

  Request* to_write;
  to_write = req.clone();

  delete wrapped;
  return to_write;
}

void PIR::jump_table(Message* m)
{

  th_assert(m, "null message...");

  // TODO: This should probably be a jump table.
  // PL: NOTE: if there is now #ifdef MSG_DEBUG for some tags, this is because
  // this is done in the function which handles this type of tags.
  switch (m->tag())
  {

  case Request_tag:
    //fprintf(stderr, "A");
    gen_handle<Request> (m);
    break;

  case Pre_prepare_tag:
    //fprintf(stderr, "B");
    gen_handle<Pre_prepare> (m);
    break;

  case Prepare_tag:
    //fprintf(stderr, "C");
    gen_handle<Prepare> (m);
    break;

  case Commit_tag:
    //fprintf(stderr, "D");
    gen_handle<Commit> (m);
    break;

  case Checkpoint_tag:
    //fprintf(stderr, "E");
#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i (primary=%i) (view %qd) handles a Checkpoint\n", this->id(), primary(), view());
#endif
    gen_handle<Checkpoint> (m);
    break;

  case New_key_tag:
    //fprintf(stderr, "F");
#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i handles a New key\n", this->id());
#endif
    gen_handle<New_key> (m);
    break;

  case View_change_ack_tag:
    //fprintf(stderr, "G");
    gen_handle<View_change_ack> (m);
    break;

  case Status_tag:
    //fprintf(stderr, "H");
#ifdef MSG_DEBUG
    // there are too many status messages
    //fprintf(stderr, "Replica %i handles a Status\n", this->id());
#endif

    if (status_messages_badly_needed)
    {
      gen_handle<Status> (m);
    }
    else
    {
      //for now, just add this message to status_pending
      s_identity = ((Status*) m)->id();
      th_assert(s_identity >= 0 && s_identity < num_replicas,
          "meaningless s_identity\n");
      if (status_pending[s_identity] != NULL)
      {
        delete status_pending[s_identity];
        //fprintf(stderr, "removed older status from %d to status_pending\n",identity);
      }
      //fprintf(stderr, "added status from %d to status_pending\n",identity);
      status_pending[s_identity] = m;
    }
    break;

  case Fetch_tag:
    //fprintf(stderr, "I");
#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i (primary=%i) (view %qd) handles a Fetch\n", this->id(), primary(), view());
#endif
    gen_handle<Fetch> (m);
    break;

  case Reply_tag:
    gen_handle<Reply> (m);
    break;

  case Meta_data_tag:
    //fprintf(stderr, "O");
#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i (primary=%i) (view %qd) handles a Meta data\n", this->id(), primary(), view());
#endif
    gen_handle<Meta_data> (m);
    break;

  case Meta_data_d_tag:
    //fprintf(stderr, "P");
#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i (primary=%i) (view %qd) handles a Meta_data_d\n", this->id(), primary(), view());
#endif
    gen_handle<Meta_data_d> (m);
    break;

  case Data_tag:
    //fprintf(stderr, "Q");
#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i  (primary=%i) (view %qd) handles a Data\n", this->id(), primary(), view());
#endif
    gen_handle<Data> (m);
    break;

  case View_change_tag:
    //fprintf(stderr, "R");
    gen_handle<View_change> (m);
    break;

  case New_view_tag:
    //fprintf(stderr, "S");
    gen_handle<New_view> (m);
    break;

  case Wrapped_request_tag:
    //fprintf(stderr, "**** Main Thread should not receive wrapped requests...");

#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i (primary=%i) (view %qd) handles a Wrapped request\n", this->id(), primary(), view());
#endif

    break;

  default:
    // Unknown message type.
    //fprintf(stderr, "**** Received garbage, deleting\n");
#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i (primary=%i) (view %qd) handles an unknown message\n", this->id(), primary(), view());
#endif
    delete m;
  }
}

void PIR::handle(Protocol_instance_change* m)
{
  fprintf(stderr,
      "PIR %i_%i has received a Protocol_instance_change (%d, %qd)\n",
      protocol_instance_id, id(), m->sender_id(), m->change_id());

  send_view_change();
}

void PIR::handle(Wrapped_request *m)
{
  // create and send a pre-prepare (if I am the primary) from the requests contained in the Wrapped_request m
#ifdef MSG_DEBUG
  fprintf(stderr, "PIR %i_%i handles a Wrapped request %qd of size %i.\n",
      protocol_instance_id, id(), m->seqno(), m->size());
#endif

  Wrapped_request::Requests_iter iter(m);
  Request req;

  while (iter.get(req))
  {
#ifdef MSG_DEBUG
    fprintf(
        stderr,
        "[PIR %i_%i] Reading request %qd of size %i from %i\n",protocol_instance_id, id(),
        req.request_id(), req.size(), req.client_id());
#endif

    Request *r = req.clone();
    rqueue.append(r);
  }

#ifdef MSG_DEBUG
  //fprintf(stderr, "[PIR %i_%i] In handle(Wrapped_request), rqueue.size=%i\n",
  //    protocol_instance_id, id(), rqueue.size());
#endif

  if (id() == primary())
  {
#ifdef MSG_DEBUG
    fprintf(stderr, "[PIR %i_%i] Associating seqno %qd to Wrapped request %qd\n",
        protocol_instance_id, id(), seqno+1, m->seqno());
#endif

    send_pre_prepare();
  }

  delete m;
}

// return true if the replica has locally received all
// the requests in the PP; false otherwise
bool PIR::got_all_requests_of_pp(Pre_prepare *pp)
{
  Pre_prepare::Requests_iter iter(pp);
  Request req;

  th_assert(id() != primary(), "got_all_requests_of_pp called by the primary");

  //ATTACK: the faulty instance does not wait for all the requests (otherwise its memory conso explodes)
#ifdef LIMIT_REQ_LIST_USAGE
  if (protocol_instance_id == FAULTY_PROTOCOL_INSTANCE) {
      return true;
  }
#endif

  while (iter.get(req))
  {
    if (!rlist->look_for_request(&req)) {
      // request is missing
#ifdef MSG_DEBUG
      fprintf(stderr, "[got_all_requests_of_pp] {%qd} I do not have request %qd from client %d\n", pp->seqno(), req.request_id(), req.client_id());
#endif
      return false;
    }
  }

#ifdef MSG_DEBUG
  fprintf(stderr, "[got_all_requests_of_pp] I have all requests for PP %qd\n", pp->seqno());
#endif

  return true;
}


// return true if the PP with seqno s is already pending, false otherwise
bool PIR::pp_already_pending(Seqno s) {
  // delete all pending PP that will be truncated and associated requests
  struct pre_prepare_in_list *ppil;
  struct list_head *pos;

  list_for_each(pos, &pending_pp)
  {
    ppil = list_entry(pos, struct pre_prepare_in_list, link);

    if (ppil->pp && ppil->pp->seqno() == s)
    {
      return true;
    }
  }

  return false;
}

void PIR::handle_request(Request *req)
{
#ifdef MSG_DEBUG
  fprintf(stderr, "[handle_request] Received request %qd from client %d\n", req->request_id(), req->client_id());
#endif

  // changing view or fetching state: forget the requests we receive
  if (!has_new_view()) {
      delete req;
      return;
  }

  if (id() == primary())
  {
#ifdef LIMIT_REQ_LIST_USAGE
    if (protocol_instance_id == FAULTY_PROTOCOL_INSTANCE) {
      rqueue.append(req);
    } else {
#endif
      rlist->add_request_for_primary(req);
#ifdef LIMIT_REQ_LIST_USAGE
    }
#endif
    send_pre_prepare();
  }
  else
  {
    //ATTACK: the faulty instance does not wait for all the requests (otherwise its memory conso explodes)
#ifdef LIMIT_REQ_LIST_USAGE
    if (protocol_instance_id == FAULTY_PROTOCOL_INSTANCE) {
      delete req;
      return;
    }
#endif

#ifdef FAIRNESS_ADAPTIVE
    fprintf(stderr, "Add request (%d, %qd) at %qd\n", req->client_id(), req->request_id(), diffTime(currentTime(), 0));
#endif
    rlist->add_request(req);

    // I am fetching new state from the other replicas.
    // Do not bother with the pending PP, I cannot handle
    // them right now
    if (state.in_fetch_state()) {
#ifdef MSG_DEBUG
      fprintf(stderr, "In fetch state: cannot handle req right now\n");
#endif
      return;
    }

    // For all pending PP, is this request the last one to be received?
    // If yes, then send the PP.
    struct pre_prepare_in_list *ppil;
    struct list_head *pos, *n;
    Pre_prepare *pp = NULL;
    list_for_each_safe(pos, n, &pending_pp)
    {
      ppil = list_entry(pos, struct pre_prepare_in_list, link);

      if (got_all_requests_of_pp(ppil->pp))
      {
        pp = ppil->pp;
        list_del(pos);
        delete ppil;
        break;
      } else {
        // Do not consider PP with a greater seqno 
        break;
      }
    }

    if (pp)
      handle(pp, true);
  }
}

void PIR::handle(Request *m)
{

  //if (print_stuff)
  //    fprintf(stderr, "\t\t\t\t\t\thandling request\n");

  int cid = m->client_id();
  bool ro = m->is_read_only();
  Request_id rid = m->request_id();

  /*
   fprintf(stderr, "Replica %i (Primary=%i) handles request %qu from client %i\n", id(),
   primary(), rid, cid);
   */

  // [attacks] Bias on clients?
  if (clientBias(m))
  {
    delete m;
    return;
  }

  if (has_new_view())
  {

    // Replica's requests must be signed and cannot be read-only.
    if (!is_replica(cid) || (m->is_signed() & !ro))
    {
      if (ro)
      {
        // Read-only requests.
        if (execute_read_only(m) || !ro_rqueue.append(m))
          delete m;

        return;
      }
      Request_id last_rid = replies.req_id(cid);
      if (last_rid < rid)
      {
        /*
         printf(
         "Replica %i, Primary=%i, request %qu from client %i: The request has not been executed\n",
         id(), primary(), rid, cid);
         */

        // Request has not been executed.
        if (id() == primary())
        {
          if (!rqueue.in_progress(cid, rid, v))
          {
            if (rqueue.append(m))
            {
              //      fprintf(stderr, "RID %qd. ", rid);
              /*
               printf(
               "Replica %i, Primary=%i, request %qu from client %i: Sending the pre-prepare\n",
               id(), primary(), rid, cid);
               */

              send_pre_prepare();
              return;
            }

            /*
             printf(
             "Replica %i, Primary=%i, request %qu from client %i: rqueue.append(m) has failed\n",
             id(), primary(), rid, cid);
             */
          }
          else
          {
            /*
             printf(
             "Replica %i, Primary=%i, request %qu from client %i: rqueue.in_progress(%i, %qu, %qd) is true\n",
             id(), primary(), rid, cid, cid, rid, v);
             */
          }

        }
        else
        {
          if (m->size() > Request::big_req_thresh && brt.add_request(m))
            return;

          if (rqueue.append(m))
          {
            if (!limbo)
            {
              /*
               * RRBFT: everyone receives the request, but it is not forwarded by the PIRs to their primary
               Req_queue tmp; //req_queue used just to store the request, because wrapped request constructor takes a request queue as a parameter
               //It can be easily modified to accept a request* instead of a request queue, but I prefer to do exactly the same things
               //that are done for the generation of a Pre_prepare message.
               Request *cloned = m->clone();
               tmp.append(cloned);
               Wrapped_request* wrapped = new Wrapped_request((Seqno) id(),
               (View) 1, tmp);
               #ifdef MSG_DEBUG
               fprintf(stderr, "Replica %i, Primary=%i, request %qu from client %i: Sending a wrapped request to the primary\n",
               id(), primary(), rid, cid);
               #endif

               Message *m = (Message*) wrapped;
               sendMsg(snd_socks[primary()], m->contents(), m->size());

               delete wrapped;

               //vtimer->start(); //??? necessary? useful?
               */
            }
            return;
          }
        }
      }
      else if (last_rid == rid)
      {
        /*
         printf(
         "Replica %i, Primary=%i, request %qu from client %i: Retransmitting the reply\n",
         id(), primary(), rid, cid);
         */

        // Retransmit reply.
        replies.send_reply(cid, view(), id());

        if (id() != primary() && !replies.is_committed(cid) && rqueue.append(m))
        {
          //fprintf(stderr, "vtimer restart (5)\n");
          vtimer->start();
          return;
        }
      }
    }
  }
  else
  {
    if (m->size() > Request::big_req_thresh && !ro && brt.add_request(m, false))
    {
      th_assert(false, "big requests are disabled");
      return;
    }
  }

  delete m;
}

void PIR::send_pre_prepare(bool force_send)
{
#ifdef MSG_DEBUG
  fprintf(stderr, "[PIR %i_%i] going to send pre prepare\n",
      protocol_instance_id, id());
#endif
  if (!has_new_view())
  {
    fprintf(stderr,
        "In send_pre_prepare(), has_new_view() is false. Should not be here!!!!!\n");
    return;
  }
  th_assert(primary() == id(), "Non-primary called send_pre_prepare");

  /*
  // if the parameter byz_pre_prepare_delay is not 0, this
  // is a byzantine replica that waits a delay of
  // byz_pre_prepare_delay us before sending the pre_prepare
  if (protocol_instance_id == FAULTY_PROTOCOL_INSTANCE && id() == 0 && byz_pre_prepare_delay
      && !force_send)
  {
    //fprintf(stderr, "restart timer @ %qd usec\n", diffTime(rdtsc(), 0));
    delay_pre_prepare_timer->restart();
    return;
  }
  */

  bool test;
#ifdef LIMIT_REQ_LIST_USAGE
  if (protocol_instance_id == FAULTY_PROTOCOL_INSTANCE) {
    test = ((force_send || rqueue.size() > 0) && seqno + 1 <= last_executed
            + congestion_window && seqno + 1 <= max_out + last_stable
            && has_new_view());
  } else {
#endif
    test = ((force_send || !rlist->reqs_for_primary_empty()) && seqno + 1 <= last_executed
            + congestion_window && seqno + 1 <= max_out + last_stable
            && has_new_view());
#ifdef LIMIT_REQ_LIST_USAGE
  }
#endif

  // If rqueue is empty there are no requests for which to send
  // pre_prepare and a pre-prepare cannot be sent if the seqno excedes
  // the maximum window or the replica does not have the new view.
  if (test)
  {
    //  fprintf(stderr, "requeu.size = %d\n", rqueue.size());
    //  if (seqno % checkpoint_interval == 0)
    //if (print_stuff)
#ifdef MSG_DEBUG
    fprintf(stderr, "SND: PRE-PREPARE seqno= %qd last_stable= %qd\n", seqno + 1, last_stable);
#endif

    //fprintf(stderr, "BATCH_SIZE_PIR %i: rqueue.size=%i\n",protocol_instance_id,rqueue.size());
//#ifdef MSG_DEBUG
    if (nbreqs >= 100000)
    {
      fprintf(stderr, "Avg batch sz: %f\n", (float) nbreqs / (float) nbrounds);
      nbreqs = nbrounds = 0;
    }
//#endif

    // Create new pre_prepare message for set of requests
    // in rqueue, log message and multicast the pre_prepare.
    seqno++;
    nb_batch_sent++;

    Pre_prepare *pp;
    long old_size;
#ifdef LIMIT_REQ_LIST_USAGE
    if (protocol_instance_id == FAULTY_PROTOCOL_INSTANCE) {
      old_size = rqueue.size();
      sum_batch_size += rqueue.size();
      pp = new Pre_prepare(view(), seqno, rqueue);
      nbreqs += old_size - rqueue.size();
    } else {
#endif
      old_size = rlist->reqs_for_primary_size;
      sum_batch_size += rlist->reqs_for_primary_size;
      pp = new Pre_prepare(view(), seqno, rlist);
      nbreqs += old_size - rlist->reqs_for_primary_size;
#ifdef LIMIT_REQ_LIST_USAGE
    }
#endif

    nbrounds++;
    // TODO: should make code match my proof with request removed
    // only when executed rather than removing them from rqueue when the
    // pre-prepare is constructed.

    //if (print_stuff)
    //fprintf(stderr, "SND:  pp: (%qd, %qd),  last stable: %qd\n",seqno, pp->view(),  last_stable);
#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i, Primary=%i: Sending a pre prepare with seqno= %qd to all replicas at time=%qd\n", id(), primary(), pp->seqno(), diffTime(rdtsc(), 0));
#endif

    if (protocol_instance_id == FAULTY_PROTOCOL_INSTANCE && id() == 0 && byz_pre_prepare_delay)
    {
      struct delayed_pp *dpp = new struct delayed_pp;
      dpp->pp = pp;
      dpp->t = rdtsc();
      list_add_tail(&(dpp->link), &delayed_pps);
      //fprintf(stderr, "adding pp @ %qd usec\n", diffTime(dpp->t, 0));
    } else {
      plog.fetch(seqno).add_mine(pp);
      send(pp, All_replicas);
    }
    
    //    vtimer->stop(); //not in old pbft, so commented here
  }
  else
  {
    /*
     fprintf(stderr, "Replica %i, Primary=%i: pre prepare %qd has not been sent\n", id(),
     primary(), seqno);
     */
#ifdef MSG_DEBUG
    fprintf(
        stderr,
        "force_send=%s, rlist.is_empty()=%s, seqno=%qd, last_executed=%qd, congestion_window=%i, max_out=%i, last_stable=%qd, has_new_view()=%s\n",
        (force_send) ? "true" : "false", (rlist->reqs_for_primary_empty() ? "true" : "false"), seqno, last_executed,
        congestion_window, max_out, last_stable, (has_new_view()) ? "true"
        : "false");
#endif

  }

}

template<class T>
bool PIR::in_w(T *m)
{
  const Seqno offset = m->seqno() - last_stable;

  if (offset > 0 && offset <= max_out)
    return true;

  if (offset > max_out && m->verify())
  {
    // Send status message to obtain missing messages. This works as a
    // negative ack.
#ifdef MSG_DEBUG
      fprintf(stderr, "[%s] Gonna send_status\n", __PRETTY_FUNCTION__);
#endif
    send_status();
  }

  return false;
}

template<class T>
bool PIR::in_wv(T *m)
{
  const Seqno offset = m->seqno() - last_stable;

  if (offset > 0 && offset <= max_out && m->view() == view())
    return true;

  if ((m->view() > view() || offset > max_out) && m->verify())
  {
    // Send status message to obtain missing messages. This works as a
    // negative ack.
#ifdef MSG_DEBUG
      fprintf(stderr, "[%s] Gonna send_status because %qd > %qd || %qd > %i\n", __PRETTY_FUNCTION__, m->view(), view(), offset, max_out);
#endif
    send_status();
  }

  return false;
}

void PIR::handle(Pre_prepare *m, bool force_send)
{
  const Seqno ms = m->seqno();
  View replica_sender_view = m->view();
  int replica_sender_id = m->id();

#ifdef MSG_DEBUG
  fprintf(stderr, "Replica %i (primary %i) handles a PrePrepare with seqno= %qd from replica %i in view %qd which contains requests\n", this->id(), primary(), ms, m->id(), m->view());

  /*
   Pre_prepare::Requests_iter iter(m);
   Request req;

   while (iter.get(req))
   {
   fprintf(stderr, " %qu (%i)", req.request_id(), req.client_id());
   }
   fprintf(stderr, "\n");
   */
#endif

  //if(print_stuff)
  //  fprintf(stderr, "\tRCV: pp (%qd, %qd) from %d\n",ms,view(),m->id());

  Byz_buffer b;

  b.contents = m->choices(b.size);

  if (in_wv(m))
  {
    if (ms > low_bound)
    {
      if (has_new_view())
      {
        Prepared_cert& pc = plog.fetch(ms);

#ifdef MSG_DEBUG
        /*
         fprintf(stderr, 
         "\tReplica %i (primary %i) handles a PrePrepare with seqno= %qd from replica %i in view %qd with quorum=%i/%i\n",
         this->id(), primary(), ms, m->id(), m->view(), pc.num_correct(),
         pc.num_complete());
         */
#endif

        // Only accept message if we never accepted another pre-prepare
        // for the same view and sequence number and the message is valid.
        if (force_send || pc.add(m))
        {
#ifdef DELAY_ADAPTIVE
          received_pre_prepares++;
#endif
          //      fprintf(stderr, "\t\t\t\tsending prepare\n");

          /*
           fprintf(stderr, "\tReplica %i (primary %i) sends a Prepare\n", this->id(),
           primary());
           */

          if (!force_send && id() != primary())
          {
            // do I have all the requests of the PP?
            // Yes -> send the Prepare
            // No -> save the PP
            if (!got_all_requests_of_pp(m))
            {
#ifdef MSG_DEBUG
              fprintf(stderr, "cannot send prepare right now\n");
#endif

              if (!pp_already_pending(ms)) {
                struct pre_prepare_in_list *ppil = new struct pre_prepare_in_list;
                ppil->pp = m;
#ifdef MSG_DEBUG
                fprintf(stderr, "handle(PP): adding pending PP %qd\n", ms);
#endif
                list_add_tail(&(ppil->link), &pending_pp);
              }

              return;
            }
          }

#ifdef MSG_DEBUG
          fprintf(stderr, "[handle_pre_prepare] Oustanding PP %qd has been handled\n", ms);
#endif

          send_prepare(pc);
          if (pc.is_complete())
          {
            send_commit(ms);
          }
          else
          {
              /*
             fprintf(stderr, 
             "\tReplica %i (primary %i) handles a PrePrepare with seqno= %qd from replica %i in view %qd, quorum not complete\n",
             this->id(), primary(), ms, replica_sender_id,
             replica_sender_view);
             */
          }
        }
        else
        {
            /*
           fprintf(stderr, 
           "\tReplica %i (primary %i) handles a PrePrepare with seqno= %qd from replica %i in view %qd, failed to add the pre-prepare\n",
           this->id(), primary(), ms, replica_sender_id, replica_sender_view);
        */
           }

        return;
      }
      else
      {
          /*
         fprintf(stderr, 
         "\tReplica %i (primary %i) handles a PrePrepare with seqno= %qd from replica %i in view %qd, has_new_view() is false\n",
         this->id(), primary(), ms, m->id(), m->view());
      */
         }
    }
    else
    {
        /*
       fprintf(stderr, 
       "\tReplica %i (primary %i) handles a PrePrepare with seqno= %qd from replica %i in view %qd, %qd <= %qd\n",
       this->id(), primary(), ms, m->id(), m->view(), ms, low_bound);
    */
       }
  }
  else
  {
      /*
     fprintf(stderr, 
     "\tReplica %i (primary %i) handles a PrePrepare with seqno= %qd from replica %i in view %qd while view=%qd and last_stable= %qd\n",
     this->id(), primary(), ms, m->id(), m->view(), view(), last_stable);
  */
     }

  if (!has_new_view())
  {
    // This may be an old pre-prepare that replica needs to complete
    // a view-change.
    vi.add_missing(m);
    return;
  }

  delete m;
}

void PIR::send_prepare(Prepared_cert& pc)
{
  if (pc.my_prepare() == 0 && pc.is_pp_complete())
  {
    // Send prepare to all replicas and log it.
    Pre_prepare* pp = pc.pre_prepare();
    Prepare *p = new Prepare(v, pp->seqno(), pp->digest());

    //if (print_stuff)
    //  fprintf(stderr, "\tSND prepare (%qd, %qd)\n", p->seqno(), p->view());

#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i (primary=%i): Sending a prepare to all replicas\n", id(), primary());
#endif
    send(p, All_replicas);
    pc.add_mine(p);
  }
}

void PIR::send_commit(Seqno s)
{

  // Executing request before sending commit improves performance
  // for null requests. May not be true in general.
  //PL: do not do that for RRBFT
  /*
   if (s == last_executed + 1)
   execute_prepared(false);
   */

  Commit* c = new Commit(view(), s);
  //if (print_stuff)
  //  fprintf(stderr, "\t\tSND commit (%qd, %qd)\n", s, c->view());
#ifdef MSG_DEBUG
  fprintf(stderr, "Replica %i (primary=%i): Sending a commit %qd to all replicas\n", id(), primary(), c->seqno());
#endif
  send(c, All_replicas);

  if (s > last_prepared)
    last_prepared = s;

  Certificate < Commit > &cs = clog.fetch(s);
  if (cs.add_mine(c) && cs.is_complete())
  {
#ifdef MSG_DEBUG
    fprintf(
        stderr,
        "\t\tPIR %i_%i (primary=%i): Sends commit with seqno= %qd in view %qd. Commit is complete, sends batch to verifier\n",
        protocol_instance_id, this->id(), primary(), s, view());
#endif
    //execute_committed();

    send_batch_to_verifier();
  }
}

void PIR::handle(Prepare *m)
{
  const Seqno ms = m->seqno();

#ifdef MSG_DEBUG
  fprintf(stderr, "Replica %i (primary=%i): handles a Prepare with seqno= %qd from %i in view %qd\n", this->id() , primary(), ms, m->id(), m->view());
#endif

  //if (print_stuff)
  //  fprintf(stderr, "\t\tRCV p (%qd, %qd) from %d\n", ms, m->view(), m->id());

  // Only accept prepare messages that are not sent by the primary for
  // current view.
  if (in_wv(m))
  {
    if (ms > low_bound)
    {
      if (primary() != m->id())
      {
        if (has_new_view())
        {
          Prepared_cert& ps = plog.fetch(ms);

          /*
           printf(
           "\tReplica %i (primary=%i): handles a Prepare with seqno= %qd and quorum=%i/%i\n",
           this->id(), primary(), ms, ps.pc_num_correct(),
           ps.pc_num_complete());
           */

          if (ps.add(m))
          {
            if (ps.is_complete())
            {
              send_commit(ms);
            }
            else
            {
              // certificate not complete
                /*
               fprintf(stderr,
               "\tReplica %i (primary=%i): handles a Prepare with seqno= %qd, certificate not complete\n",
               this->id(), primary(), ms);
            */
               }
          }
          else
          {
            // failed to add
            /*
              fprintf(stderr,
             "\tReplica %i (primary=%i): handles a Prepare with seqno= %qd, failed to add the prepare\n",
             this->id(), primary(), ms);
          */
             }
          return;
        }
        else
        {
            /*
           fprintf(stderr,
           "\tReplica %i (primary=%i): handles a Prepare with seqno= %qd, has_new_view() is false\n",
           this->id(), primary(), ms);
        */
           }
      }
      else
      {
          /*
         fprintf(stderr,
         "\tReplica %i (primary=%i): handles a Prepare with seqno= %qd, %i != %i\n",
         this->id(), primary(), ms, primary(), m->id());
      */
         }
    }
    else
    {
        /*
       fprintf(stderr,
       "\tReplica %i (primary=%i): handles a Prepare with seqno= %qd, %qd <= %qd\n",
       this->id(), primary(), ms, ms, low_bound);
    */
       }
  }
  else
  {
      /*
     fprintf(stderr,
     "\tReplica %i (primary=%i): handles a Prepare with seqno= %qd while view=%qd and last_stable= %qd\n",
     this->id(), primary(), ms, view(), last_stable);
  */
     }

  if (m->is_proof() && !has_new_view())
  {
    // This may be an prepare sent to prove the authenticity of a
    // request to complete a view-change.
    vi.add_missing(m);
    return;
  }

  delete m;
  return;
}

// to mimics the execute_committed() -> execute_prepared() relation
void PIR::truly_send_batch_to_verifier(void)
{
  if (last_tentative_execute < last_executed + 1 && last_executed < last_stable
      + max_out && !state.in_fetch_state() && has_new_view())
  {
    Pre_prepare *pp = prepared(last_executed + 1);

    if (pp && pp->view() == view())
    {
      // Can execute the requests in the message with sequence number
      // last_executed+1.
      last_tentative_execute = last_executed + 1;
      th_assert(pp->seqno() == last_tentative_execute, "Invalid execution");

      send_wrapped_request_and_delete_requests_of_pp(pp, view(), last_executed+1, "truly_send_batch_to_verifier", false);

      if ((last_executed + 1) % checkpoint_interval == 0)
      {
        state.checkpoint(last_executed + 1);
        if (status_messages_badly_needed && (last_executed
            > not_deprioritize_status_before_this + max_out))
        {
          //fprintf(stderr, "****** Deprioritizing status messages\n");
          status_messages_badly_needed = false;
        }
      }
    }
  } else {
#ifdef MSG_DEBUG
    fprintf(stderr, "[PIR %i_%i] cannot truly_send_batch_to_verifier because is fetching state\n",
        protocol_instance_id, id());
#endif
  }
}

void PIR::delete_all_requests_of_pp(Pre_prepare *pp, const bool upto) {
  if (id() == primary()) {
    return;
  }

  Pre_prepare::Requests_iter iter(pp);
  Request req;

#ifdef FAIRNESS_ADAPTIVE
  long long d = diffTime(currentTime(), 0);
#endif

  while (iter.get(req))
  {
#ifdef FAIRNESS_ADAPTIVE
    fprintf(stderr, "Del request (%d, %qd) at %qd\n", req.client_id(), req.request_id(), d);
#endif
    if (upto)
      rlist->delete_request_up_to_rid(req.client_id(), req.request_id());
    else
      rlist->delete_request(req.client_id(), req.request_id());

#ifdef FAIRNESS_ADAPTIVE
    executed_seqno[req.client_id()-num_replicas] = pp->seqno();
#endif
  }
}

// The PIR has a commit certificate.
// It sends the batch (requests in rqueue) to the Verifier
void PIR::send_batch_to_verifier(void)
{
#ifdef MSG_DEBUG
  fprintf(
      stderr,
      "[PIR %i_%i] send_batch_to_verifier: last_tentative_execute=%qd, last_executed=%qd, last_stable=%qd, max_out=%i, in_fetch=%s, new_view=%s\n",
      protocol_instance_id, id(), last_tentative_execute, last_executed,
      last_stable, max_out, (state.in_fetch_state() ? "true" : "false"),
      (has_new_view() ? "true" : "false"));
#endif

  if (!state.in_fetch_state() && has_new_view())
  {
    while (1)
    {
    if (last_executed >= last_stable + max_out || last_executed < last_stable)
      return;

    Pre_prepare *pp = committed(last_executed + 1);
    //fprintf(stderr, "\t\t\tlast executed: %qd\n", last_executed);
    //fprintf(stderr, "\t\t\tlast stable: %qd\n", last_stable);
    //if (pp) fprintf(stderr, "\t\t\tpp->seqno: %qd\n", pp->seqno());
    print_stuff = false;

    if (pp && pp->view() == view() && (id() == primary() || got_all_requests_of_pp(pp)))
    {
      truly_send_batch_to_verifier();

      // Can execute the requests in the message with sequence number
      // last_executed+1.
      last_executed = last_executed + 1;
      //fprintf(stderr, "\t\t\tlast executed: %qd\n", last_executed);
      //fprintf(stderr, "\t\t\tlast stable: %qd\n", last_stable);
      //fprintf(stderr, "\t\t\tpp->seqno: %qd\n", pp->seqno());
      //  fprintf(stderr, ":):):):):):):):) Set le = %d\n", last_executed);
      th_assert(pp->seqno() == last_executed, "Invalid execution");

      // fprintf(stderr, "PP %qd ordered\n", pp->seqno());

      // Send and log Checkpoint message for the new state if needed.
      if (last_executed % checkpoint_interval == 0)
      {
        Digest d_state;
        state.digest(last_executed, d_state);
        Checkpoint *e = new Checkpoint(last_executed, d_state);
        Certificate < Checkpoint > &cc = elog.fetch(last_executed);
        cc.add_mine(e);
        if (cc.is_complete())
        {
          mark_stable(last_executed, true);
          //      fprintf(stderr, "EXEC call MS %qd!!!\n", last_executed);
        }
        //    else
        //      fprintf(stderr, "CP exec %qd not yet. ", last_executed);

        //e = cc.mine(); //not in PBFT
        //th_assert(e, "I just added my checkpoint"); //not in PBFT
#ifdef MSG_DEBUG
        fprintf(stderr, "Sending a checkpoint to all replicas\n");
#endif
        send(e, All_replicas);
        //    fprintf(stderr, ">>>>Checkpointing "); d_state.print(); fprintf(stderr, " <<<<\n"); fflush(stdout);
      }
    }
    else
    {
      break;
    }
  }

    if (id() == primary()) {
      bool has_reqs; 
#ifdef LIMIT_REQ_LIST_USAGE
      if (protocol_instance_id == FAULTY_PROTOCOL_INSTANCE) {
        has_reqs = (rqueue.size() > 0);
      } else {
#endif
        has_reqs = !rlist->reqs_for_primary_empty();
#ifdef LIMIT_REQ_LIST_USAGE
      }
#endif

      if (has_reqs)
      {
        send_pre_prepare();
      }
    }
  } else {
#ifdef MSG_DEBUG
    fprintf(stderr, "[PIR %i_%i] cannot send_batch_to_verifier because is fetching state\n",
        protocol_instance_id, id());
#endif
  }
}

void PIR::handle(Commit *m)
{
  const Seqno ms = m->seqno();

#ifdef MSG_DEBUG
  int replica_sender_id = m->id();
  View replica_sender_view = m->view();
#endif

#ifdef MSG_DEBUG
  fprintf(
      stderr,
      "Replica %i (primary=%i): handles a Commit with seqno= %qd from %i in view %qd\n",
      this->id(), primary(), ms, replica_sender_id, replica_sender_view);
#endif

  //if(print_stuff)
  //  fprintf(stderr, "\t\t\tRCV c (%qd, %qd) from %d\n", ms, m->view(), m->id());

  // Only accept messages with the current view.  TODO: change to
  // accept commits from older views as in proof.
  //Seqno commit_seqno = m->seqno();
  if (in_wv(m))
  {
    if (ms > low_bound)
    {
      Certificate < Commit > &cs = clog.fetch(m->seqno());

#ifdef MSG_DEBUG
      /*
       printf(
       "\tReplica %i (primary=%i): handles a Commit with seqno= %qd, quorum size = %i/%i\n",
       this->id(), primary(), ms, cs.num_correct(), cs.num_complete());
       */
#endif

      if (cs.add(m))
      {
        if (cs.is_complete())
        {
#ifdef MSG_DEBUG
          fprintf(
              stderr,
              "\t\tPIR %i_%i (primary=%i): handles a Commit with seqno= %qd from %i in view %qd, sends batch to verifier\n",
              protocol_instance_id, this->id(), primary(), ms,
              replica_sender_id, replica_sender_view);
#endif
          //execute_committed();
          send_batch_to_verifier();
        }
        else
        {
#ifdef MSG_DEBUG
          /*
           printf(
           "\t\tReplica %i (primary=%i): handles a Commit with seqno= %qd from %i in view %qd, quroum not complete\n",
           this->id(), primary(), ms, replica_sender_id, replica_sender_view);
           */
#endif
        }
      }
      else
      {
        /*
         printf(
         "\tReplica %i (primary=%i): handles a Commit with seqno= %qd from %i in view %qd, failed to add commit\n",
         this->id(), primary(), ms, replica_sender_id, replica_sender_view);
         */
      }
      return;
    }
    else
    {
      /*
       printf(
       "\tReplica %i (primary=%i): handles a Commit with seqno= %qd from %i in view %qd, %qd <= %qd\n",
       this->id(), primary(), ms, m->id(), m->view(), ms, low_bound);
       */
    }
  }
  else
  {
    /*
     printf(
     "\tReplica %i (primary=%i): handles a Commit with seqno= %qd from %i in view %qd while view=%qd and last_stable= %qd\n",
     this->id(), primary(), ms, m->id(), m->view(), view(), last_stable);
     */
  }

  delete m;
  return;
}

void PIR::handle(Checkpoint *m)
{
  const Seqno ms = m->seqno();
  //if (print_stuff)
  //  fprintf(stderr, "\t\t\t\tRCV CP %qd from %d.  last_stable: %qd, last_exec: %qd\n",ms, m->id(), last_stable, last_executed);
  if (ms > last_stable)
  {
    //fprintf(stderr, "CHECKPOINT 1\n");
    if (ms <= last_stable + max_out)
    {
      //fprintf(stderr, "CHECKPOINT 2\n");
      // Checkpoint is within my window.  Check if checkpoint is
      // stable and it is above my last_executed.  This may signal
      // that messages I missed were garbage collected and I should
      // fetch the state
      bool late = m->stable() && last_executed < ms;

      //fprintf(stderr, "CHECKPOINT 3a\n");
      if (!late)
      {
        //fprintf(stderr, "CHEcKPOINT 4\n");
        Certificate < Checkpoint > &cs = elog.fetch(ms);
        if (cs.add(m) && cs.mine() && cs.is_complete())
        {
          //fprintf(stderr, "CHECKPOINT 5\n");
          // I have enough Checkpoint messages for m->seqno() to make it stable.
          // Truncate logs, discard older stable state versions.
          //    fprintf(stderr, "CP MSG call MS %qd!!!\n", last_executed);
          mark_stable(ms, true);
        }
        return;
      }
    }

    if (m->verify())
    {
      //fprintf(stderr, "CHECKPOINT 6\n");
      // Checkpoint message above my window.

      if (!m->stable())
      {
        //fprintf(stderr, "CHECKPOINT 7\n");
        // Send status message to obtain missing messages. This works as a
        // negative ack.
#ifdef MSG_DEBUG
      fprintf(stderr, "[%s] Gonna send_status\n", __PRETTY_FUNCTION__);
#endif
        send_status();
        delete m;
        return;
      }

      // Stable checkpoint message above my last_executed.
      Checkpoint *c = sset.fetch(m->id());
      if (c == 0 || c->seqno() < ms)
      {
        //fprintf(stderr, "CHECKPOINT 8\n");
        delete sset.remove(m->id());
        sset.store(m);
        if (sset.size() > f())
        {
          //fprintf(stderr, "CHECKPOINT 9\n");
          if (last_tentative_execute > last_executed)
          {
            // Rollback to last checkpoint
            //fprintf(stderr, "CHECKPOINT 10\n");
            th_assert(!state.in_fetch_state(), "Invalid state");
            Seqno rc = state.rollback();
            last_tentative_execute = last_executed = rc;
            //      fprintf(stderr, ":):):):):):):):) Set le = %d\n", last_executed);
          }
          //fprintf(stderr, "CHECKPOINT 10a\n");
          // Stop view change timer while fetching state. It is restarted
          // in new state when the fetch ends.
          vtimer->stop();
          state.start_fetch(last_executed);
        }
        //fprintf(stderr, "checkpoint 11\n");
        return;
      }
    }
  }
  delete m;
  return;
}

void PIR::handle(New_key *m)
{
  //fprintf(stderr, "\t\t\t\t\t\tnew key\n");
  if (!m->verify())
  {
    //fprintf(stderr, "BAD NKEY from %d\n", m->id());
  }
  delete m;
}

void PIR::handle(Status* m)
{
  //fprintf(stderr, "\t\t\t\t\t\tstatus from %d\n", m->id());
  static const int max_ret_bytes = 65536;

  if (m->verify())
  {
    Time current;
    Time *t;
    current = currentTime();
    Principal *p = node->i_to_p(m->id());

    // Retransmit messages that the sender is missing.
    if (last_stable > m->last_stable() + max_out)
    {
      // Node is so out-of-date that it will not accept any
      // pre-prepare/prepare/commmit messages in my log.
      // Send a stable checkpoint message for my stable checkpoint.
      Checkpoint *c = elog.fetch(last_stable).mine(&t);
      //th_assert(c != 0 && c->stable(), "Invalid state");
      if (c != 0 && c->stable()) // randomly triggered assertion replaced with this line...
        retransmit(c, current, t, p);
      delete m;
      return;
    }

    // Retransmit any checkpoints that the sender may be missing.
    int max = MIN(last_stable, m->last_stable()) + max_out;
    int min = MAX(last_stable, m->last_stable() + 1);
    for (Seqno n = min; n <= max; n++)
    {
      if (n % checkpoint_interval == 0)
      {
        Checkpoint *c = elog.fetch(n).mine(&t);
        if (c != 0)
        {
          retransmit(c, current, t, p);
          th_assert(n == last_stable || !c->stable(), "Invalid state");
        }
      }
    }

    if (m->view() < v)
    {
      // Retransmit my latest view-change message
      View_change* vc = vi.my_view_change(&t);
      if (vc != 0)
      {
        //printf(
        //    "Replica %i, primary %i, view %qd: retransmitting View change for view %qd (1)\n",
        //    id(), primary(), view(), vc->view());

        retransmit(vc, current, t, p);
      }
      delete m;
      return;
    }

    if (m->view() == v)
    {
      if (m->has_nv_info())
      {
        min = MAX(last_stable + 1, m->last_executed() + 1);
        for (Seqno n = min; n <= max; n++)
        {
          if (m->is_committed(n))
          {
            // No need for retransmission of commit or pre-prepare/prepare
            // message.
            continue;
          }

          Commit *c = clog.fetch(n).mine(&t);
          if (c != 0)
          {
            retransmit(c, current, t, p);
          }

          if (m->is_prepared(n))
          {
            // No need for retransmission of pre-prepare/prepare message.
            continue;
          }

          // If I have a pre-prepare/prepare send it, provided I have sent
          // a pre-prepare/prepare for view v.
          if (primary() == node_id)
          {
            Pre_prepare *pp = plog.fetch(n).my_pre_prepare(&t);
            if (pp != 0)
            {
              retransmit(pp, current, t, p);
            }
          }
          else
          {
            Prepare *pr = plog.fetch(n).my_prepare(&t);
            if (pr != 0)
            {
              retransmit(pr, current, t, p);
            }
          }
        }

        if (id() == primary())
        {
          // For now only primary retransmits big requests.
          Status::BRS_iter gen(m);

          int count = 0;
          Seqno ppn;
          BR_map mrmap;
          while (gen.get(ppn, mrmap) && count <= max_ret_bytes)
          {
            if (plog.within_range(ppn))
            {
              Pre_prepare_info::BRS_iter
                  gen(plog.fetch(ppn).prep_info(), mrmap);
              Request* r;
              while (gen.get(r))
              {
#ifdef MSG_DEBUG
                fprintf(stderr,
                    "Replica %i (primary=%i): Sending a request to %i\n", id(),
                    primary(), m->id());
#endif
                send(r, m->id());
                count += r->size();
              }
            }
          }
        }
      }
      else
      {
        // m->has_nv_info() == false
        if (!m->has_vc(node_id))
        {
          // p does not have my view-change: send it.
          View_change* vc = vi.my_view_change(&t);
          //View_change* vc = vi.my_view_change();
          th_assert(vc != 0, "Invalid state");

          //printf(
          //   "Replica %i, primary %i, view %qd: retransmitting View change for view %qd (2)\n",
          //   id(), primary(), view(), vc->view());

          retransmit(vc, current, t, p);
        }

        if (!m->has_nv_m())
        {
          if (primary(v) == node_id && vi.has_new_view(v))
          {
            // p does not have new-view message and I am primary: send it
            New_view* nv = vi.my_new_view(&t);
            //New_view* nv = vi.my_new_view();
            if (nv != 0)
            {
              /*
               printf(
               "Replica %i, primary %i, view %qd: retransmitting new view for view %qd\n",
               id(), primary(), view(), nv->view());
               */
              retransmit(nv, current, t, p);
            }
          }
        }
        else
        {
          if (primary(v) == node_id && vi.has_new_view(v))
          {
            // Send any view-change messages that p may be missing
            // that are referred to by the new-view message.  This may
            // be important if the sender of the original message is
            // faulty.


          }
          else
          {
            // Send any view-change acks p may be missing.
            for (int i = 0; i < num_replicas; i++)
            {
              if (m->id() == i)
                continue;
              View_change_ack* vca = vi.my_vc_ack(i);
              if (vca && !m->has_vc(i))
              {
                //printf(
                //    "Replica %i, primary %i, view %qd: retransmitting view change ack for view %qd, from %i for ack of %i\n",
                //   id(), primary(), view(), vca->view(), vca->id(),
                //   vca->vc_id());

                // View-change acks are not being authenticated
                retransmit(vca, current, &current, p);
              }
            }
          }

          // Send any pre-prepares that p may be missing and any proofs
          // of authenticity for associated requests.
          Status::PPS_iter gen(m);

          int count = 0;
          Seqno ppn;
          View ppv;
          bool ppp;
          BR_map mrmap;
          while (gen.get(ppv, ppn, mrmap, ppp))
          {
            Pre_prepare* pp = 0;
            if (m->id() == primary(v))
              pp = vi.pre_prepare(ppn, ppv);
            else
            {
              if (primary(v) == id() && plog.within_range(ppn))
                pp = plog.fetch(ppn).pre_prepare();
            }

            if (pp)
            {
              retransmit(pp, current, &current, p);

              if (count < max_ret_bytes && mrmap != ~0)
              {
                Pre_prepare_info pi;
                pi.add_complete(pp);

                Pre_prepare_info::BRS_iter gen(&pi, mrmap);
                Request* r;
                while (gen.get(r))
                {
#ifdef MSG_DEBUG
                  fprintf(stderr,
                      "Replica %i (primary=%i): Sending a request to %i\n",
                      id(), primary(), m->id());
#endif
                  send(r, m->id());
                  count += r->size();
                }
                pi.zero(); // Make sure pp does not get deallocated
              }
            }

            if (ppp)
              vi.send_proofs(ppn, ppv, m->id());
          }
        }
      }
    }
  }
  else
  {
    // It is possible that we could not verify message because the
    // sender did not receive my last new_key message. It is also
    // possible message is bogus. We choose to retransmit last new_key
    // message.  TODO: should impose a limit on the frequency at which
    // we are willing to do it to prevent a denial of service attack.
    // This is not being done right now.
    if (last_new_key != 0 && !m->verify())
    {
      //fprintf(stderr, "sending new key to %d\n", m->id());
#ifdef MSG_DEBUG
      fprintf(stderr, "Sending a last new key to %i\n", m->id());
#endif
      send(last_new_key, m->id());
    }
  }

  delete m;
}

Pre_prepare* PIR::prepared(Seqno n)
{
  Prepared_cert& pc = plog.fetch(n);
  if (pc.is_complete())
  {
    return pc.pre_prepare();
  }
  return 0;
}

Pre_prepare *PIR::committed(Seqno s)
{
  // TODO: This is correct but too conservative: fix to handle case
  // where commit and prepare are not in same view; and to allow
  // commits without prepared requests, i.e., only with the
  // pre-prepare.
  Pre_prepare *pp = prepared(s);
  if (clog.fetch(s).is_complete())
    return pp;
  return 0;
}

bool PIR::execute_read_only(Request *req)
{
  // JC: won't execute read-only if there's a current tentative execution
  // this probably isn't necessary if clients wait for 2f+1 RO responses
  if (last_tentative_execute == last_executed && !state.in_fetch_state())
  {
    // Create a new Reply message.
    Reply *rep = new Reply(view(), req->request_id(), node_id);

    // Obtain "in" and "out" buffers to call exec_command
    Byz_req inb;
    Byz_rep outb;

    inb.contents = req->command(inb.size);
    outb.contents = rep->store_reply(outb.size);

    // Execute command.
    int cid = req->client_id();
    Principal *cp = i_to_p(cid);
    int error = exec_command(&inb, &outb, 0, cid, true, (long int) 0);

    if (outb.size % ALIGNMENT_BYTES)
      for (int i = 0; i < ALIGNMENT_BYTES - (outb.size % ALIGNMENT_BYTES); i++)
        outb.contents[outb.size + i] = 0;

    if (!error)
    {
      // Finish constructing the reply and send it.
      rep->set_cid(cid);
      rep->authenticate(cp, outb.size, true);
      if (outb.size < 50 || req->replier() == node_id || req->replier() < 0)
      {
        // Send full reply.
#ifdef MSG_DEBUG
        fprintf(stderr,
            "Replica %i (primary=%i): Sending a reply to client %i\n", id(),
            primary(), cid);
#endif
        send(rep, cid);
      }
      else
      {
        // Send empty reply.
        Reply
            empty(view(), req->request_id(), node_id, rep->digest(), cp, true);
#ifdef MSG_DEBUG
        fprintf(stderr,
            "Replica %i (primary=%i): Sending an empty reply to client %i\n",
            id(), primary(), cid);
#endif
        empty.set_cid(cid);
        send(&empty, cid);
      }
    }

    delete rep;
    return true;
  }
  else
  {
    return false;
  }
}

void PIR::execute_prepared(bool committed)
{
#ifdef MSG_DEBUG
  fprintf(stderr, "[PIR %i_%i] In execute_prepared(). Should not be there!\n",
      protocol_instance_id, id());
#endif

  //  if(!committed) return;
  if (last_tentative_execute < last_executed + 1 && last_executed < last_stable
      + max_out && !state.in_fetch_state() && has_new_view())
  {
    Pre_prepare *pp = prepared(last_executed + 1);

    if (pp && pp->view() == view())
    {
      // Can execute the requests in the message with sequence number
      // last_executed+1.
      last_tentative_execute = last_executed + 1;
      th_assert(pp->seqno() == last_tentative_execute, "Invalid execution");

      // Iterate over the requests in the message, calling execute for
      // each of them.
      Pre_prepare::Requests_iter iter(pp);
      Request req;

      while (iter.get(req))
      {
        int cid = req.client_id();
        if (replies.req_id(cid) >= req.request_id())
        {
          // Request has already been executed and we have the reply to
          // the request. Resend reply and don't execute request
          // to ensure idempotence.
          replies.send_reply(cid, view(), id());
          continue;
        }

        // Obtain "in" and "out" buffers to call exec_command
        Byz_req inb;
        Byz_rep outb;
        Byz_buffer non_det;
        inb.contents = req.command(inb.size);
        outb.contents = replies.new_reply(cid, outb.size);
        non_det.contents = pp->choices(non_det.size);

        if (is_replica(cid))
        {
          // Handle recovery requests, i.e., requests from replicas,
          // differently.  TODO: make more general to allow other types
          // of requests from replicas.
          //    fprintf(stderr, "\n\n\nExecuting recovery request seqno= %qd rep id=%d\n", last_tentative_execute, cid);

          if (inb.size != sizeof(Seqno))
          {
            // Invalid recovery request.
            continue;
          }

          // Change keys. TODO: could change key only for recovering replica.
          if (cid != node_id)
            send_new_key();

          // Store seqno of execution.
          max_rec_n = last_tentative_execute;

          // Reply includes sequence number where request was executed.
          outb.size = sizeof(last_tentative_execute);
          memcpy(outb.contents, &last_tentative_execute, outb.size);
        }
        else
        {
          // Execute command in a regular request.
          //exec_command(&inb, &outb, &non_det, cid, false, exec_command_delay);
          exec_command(&inb, &outb, &non_det, cid, false, exec_command_delay);
          if (outb.size % ALIGNMENT_BYTES)
            for (int i = 0; i < ALIGNMENT_BYTES - (outb.size % ALIGNMENT_BYTES); i++)
              outb.contents[outb.size + i] = 0;
          //if (last_tentative_execute%100 == 0)
          //  fprintf(stderr, "%s - %qd\n",((node_id == primary()) ? "P" : "B"), last_tentative_execute);
        }

        // Finish constructing the reply.
        replies.end_reply(cid, req.request_id(), outb.size);

        // Send the reply. Replies to recovery requests are only sent
        // when the request is committed.
        if (outb.size != 0 && !is_replica(cid))
        {
          if (outb.size < 50 || req.replier() == node_id || req.replier() < 0)
          {
            // Send full reply.
            replies.send_reply(cid, view(), id(), !committed);
          }
          else
          {
            // Send empty reply.
            Reply empty(view(), req.request_id(), node_id, replies.digest(cid),
                i_to_p(cid), !committed);
#ifdef MSG_DEBUG
            fprintf(
                stderr,
                "Replica %i (primary=%i): Sending an empty reply to client %i\n",
                id(), primary(), cid);
#endif
            empty.set_cid(cid);
            send(&empty, cid);
          }
        }
      }

      if ((last_executed + 1) % checkpoint_interval == 0)
      {
        state.checkpoint(last_executed + 1);
        if (status_messages_badly_needed && (last_executed
            > not_deprioritize_status_before_this + max_out))
        {
          fprintf(stderr, "****** Deprioritizing status messages\n");
          status_messages_badly_needed = false;
        }
      }
    }
  }
}

void PIR::execute_committed()
{
  if (!state.in_fetch_state() && has_new_view())
  {
    while (1)
    {
      if (last_executed >= last_stable + max_out || last_executed < last_stable)
        return;

      Pre_prepare *pp = committed(last_executed + 1);
      //      if (print_stuff)
      //                         fprintf(stderr, "\t\t\tlast executed: %qd\n", last_executed);
      //fprintf(stderr, "\t\t\tlast stable: %qd\n", last_stable);
      //if (pp) fprintf(stderr, "\t\t\tpp->seqno: %qd\n", pp->seqno());
      print_stuff = false;

      if (pp && pp->view() == view())
      {
        // Tentatively execute last_executed + 1 if needed.
        execute_prepared(true);

        // Can execute the requests in the message with sequence number
        // last_executed+1.
        last_executed = last_executed + 1;
        //fprintf(stderr, "\t\t\tlast executed: %qd\n", last_executed);
        //fprintf(stderr, "\t\t\tlast stable: %qd\n", last_stable);
        //fprintf(stderr, "\t\t\tpp->seqno: %qd\n", pp->seqno());
        //  fprintf(stderr, ":):):):):):):):) Set le = %d\n", last_executed);
        th_assert(pp->seqno() == last_executed, "Invalid execution");

        // Execute any buffered read-only requests
        for (Request *m = ro_rqueue.remove(); m != 0; m = ro_rqueue.remove())
        {
          execute_read_only(m);
          delete m;
        }
        // Iterate over the requests in the message, marking the saved replies
        // as committed (i.e., non-tentative for each of them).
        Pre_prepare::Requests_iter iter(pp);
        Request req;
        while (iter.get(req))
        {
          int cid = req.client_id();
          replies.commit_reply(cid);
          if (is_replica(cid))
          {
            // Send committed reply to recovery request.
            if (cid != node_id)
            {
              replies.send_reply(cid, view(), id(), false);
            }
            else
              handle(replies.reply(cid)->copy(cid), true);
          }

          // Remove the request from rqueue if present.
          if (rqueue.remove(cid, req.request_id()))
            vtimer->stop();
        }

        // Send and log Checkpoint message for the new state if needed.
        if (last_executed % checkpoint_interval == 0)
        {
          Digest d_state;
          state.digest(last_executed, d_state);
          Checkpoint *e = new Checkpoint(last_executed, d_state);
          Certificate < Checkpoint > &cc = elog.fetch(last_executed);
          cc.add_mine(e);
          if (cc.is_complete())
          {
            mark_stable(last_executed, true);
            //      fprintf(stderr, "EXEC call MS %qd!!!\n", last_executed);
          }
          //    else
          //      fprintf(stderr, "CP exec %qd not yet. ", last_executed);

          //e = cc.mine(); //not in PBFT
          //th_assert(e, "I just added my checkpoint"); //not in PBFT
#ifdef MSG_DEBUG
          fprintf(stderr, "Sending a checkpoint to all replicas\n");
#endif
          send(e, All_replicas);
          //    fprintf(stderr, ">>>>Checkpointing "); d_state.print(); fprintf(stderr, " <<<<\n"); fflush(stdout);
        }
      }
      else
      {
        // No more requests to execute at this point.
        break;
      }
    }

    if (rqueue.size() > 0)
    {
      if (primary() == node_id)
      {
        // Send a pre-prepare with any buffered requests
        send_pre_prepare();
      }
      else
      {
        // If I am not the primary and have pending requests restart the
        // timer.
        //vtimer->start(); //necessary? useful?
      }
    }
  }
}

void PIR::update_max_rec()
{
  // Update max_rec_n to reflect new state.
  bool change_keys = false;
  for (int i = 0; i < num_replicas; i++)
  {
    if (replies.reply(i))
    {
      int len;
      char *buf = replies.reply(i)->reply(len);
      if (len == sizeof(Seqno))
      {
        Seqno nr;
        memcpy(&nr, buf, sizeof(Seqno));

        if (nr > max_rec_n)
        {
          max_rec_n = nr;
          change_keys = true;
        }
      }
    }
  }

  // Change keys if state fetched reflects the execution of a new
  // recovery request.
  if (change_keys)
    send_new_key();
}

void PIR::new_state(Seqno c)
{
  //  fprintf(stderr, ":n)e:w):s)t:a)t:e) ");
  if (vi.has_new_view(v) && c >= low_bound)
    has_nv_state = true;

  if (c > last_executed)
  {
    last_executed = last_tentative_execute = c;
    //    fprintf(stderr, ":):):):):):):):) (new_state) Set le = %d\n", last_executed);
    if (replies.new_state(&rqueue))
      vtimer->stop();

    update_max_rec();

    if (c > last_prepared)
      last_prepared = c;

    if (c > last_stable + max_out)
    {
      mark_stable(c - max_out,
          elog.within_range(c - max_out) && elog.fetch(c - max_out).mine());
    }

    // Send checkpoint message for checkpoint "c"
    Digest d;
    state.digest(c, d);
    Checkpoint* ck = new Checkpoint(c, d);
    elog.fetch(c).add_mine(ck);
    //ck = elog.fetch(c).mine(); //not in PBFT
    //th_assert(ck, " I just added my checkpoint"); //not in PBFT
#ifdef MSG_DEBUG
    fprintf(stderr, "Sending a checkpoint to all replicas\n");
#endif
    send(ck, All_replicas);
  }

  // Check if c is known to be stable.
  int scount = 0;
  for (int i = 0; i < num_replicas; i++)
  {
    Checkpoint* ck = sset.fetch(i);
    if (ck != 0 && ck->seqno() >= c)
    {
      th_assert(ck->stable(), "Invalid state");
      scount++;
    }
  }
  if (scount > f())
    mark_stable(c, true);

  if (c > seqno)
  {
    seqno = c;
  }

  // Execute any committed requests
  //PL: call send_batch_to_verifier
  //  execute_committed();
  send_batch_to_verifier();

  // Execute any buffered read-only requests
  for (Request *m = ro_rqueue.remove(); m != 0; m = ro_rqueue.remove())
  {
    execute_read_only(m);
    delete m;
  }

  if (primary() == id()) {
    // Send pre-prepares for any buffered requests
    send_pre_prepare();
  } else {
    vtimer->restart();
  }
}


void PIR::send_wrapped_request_and_delete_requests_of_pp(Pre_prepare *pp, View v, Seqno s,
    const char *fname, const bool upto)
{
  Wrapped_request *wr = new Wrapped_request(v, s, pp);
#ifdef MSG_DEBUG
  fprintf(stderr, "[PIR %i_%i] gonna send a wrapped_request %qd with %d reqs from %s\n",
      protocol_instance_id, id(), wr->seqno(), wr->client_id(), fname);
#endif
  send(wr, num_replicas);

#ifdef DEBUG_PERIODICALLY_PRINT_THR
  __sync_fetch_and_add(&nb_sent_ordered_requests, wr->client_id());
#endif

  delete wr;

  delete_all_requests_of_pp(pp, upto);

#ifdef FAIRNESS_ADAPTIVE
  for (int cid=0; cid<num_clients(); cid++) {
    if (executed_seqno[cid] > 0) {
      fprintf(stderr, "executed_seqno: %qd %d %qd\n", s, cid+num_replicas, s-executed_seqno[cid]);
    }
  }
#endif
}

#ifdef DEBUG_PERIODICALLY_PRINT_THR
static int pipes_thr_handler_id = 0;
#endif

void PIR::mark_stable(Seqno n, bool have_state)
{
  //XXXXXcheck if this should be < or <=
  if (n <= last_stable)
    return;

#ifdef MSG_DEBUG
#ifdef DEBUG_PERIODICALLY_PRINT_THR
  fprintf(stderr, "mark stable n=%qd laststable=%qd pipes_thr_nb= %d T=%f\n", n, last_stable, pipes_thr_handler_id, diffTime(currentTime(), 0) / 1000.0);
#else
  fprintf(stderr, "mark stable n=%qd laststable=%qd T=%f\n", n, last_stable, diffTime(currentTime(), 0) / 1000.0);
#endif
#endif

  last_stable = n;
  if (last_stable > low_bound)
  {
    low_bound = last_stable;
  }

  if (have_state && last_stable > last_executed)
  {
    last_executed = last_tentative_execute = last_stable;
    //    fprintf(stderr, ":):):):):):):):) (mark_stable) Set le = %d\n", last_executed);
    replies.new_state(&rqueue);
    update_max_rec();

    if (last_stable > last_prepared)
      last_prepared = last_stable;

#ifdef DEBUG_PERIODICALLY_PRINT_THR
#ifdef MSG_DEBUG
    fprintf(stderr, "mark stable n=%qd laststable=%qd pipes_thr_nb= %d T=%f\n", n, last_stable, pipes_thr_handler_id, diffTime(currentTime(), 0) / 1000.0);
#endif
#endif
  }
  //  else
  //    fprintf(stderr, "OH BASE! OH CLU!\n");

  if (last_stable > seqno)
    seqno = last_stable;

  //  fprintf(stderr, "mark_stable: Truncating plog to %ld have_state=%d\n", last_stable+1, have_state);
  // delete all pending PP that will be truncated and associated requests
  struct pre_prepare_in_list *ppil;
  struct list_head *pos, *tmp;

  list_for_each_safe(pos, tmp, &pending_pp)
  {
    ppil = list_entry(pos, struct pre_prepare_in_list, link);
    if (!ppil->pp || !ppil->pp->contents()) {
      list_del(pos);
      delete ppil;
    } else if (ppil->pp->seqno() <= last_stable) {
      Pre_prepare *pp = ppil->pp;
      send_wrapped_request_and_delete_requests_of_pp(pp, view(), pp->seqno(), "mark_stable-pending_pp", false);
      list_del(pos);
      delete ppil;
#ifdef MSG_DEBUG
      fprintf(stderr, "mark_stable(): deleting pending PP %qd\n", pp->seqno());
#endif
    }
  }

  plog.truncate(last_stable + 1);
  clog.truncate(last_stable + 1);
  vi.mark_stable(last_stable);
  elog.truncate(last_stable);
  state.discard_checkpoint(last_stable, last_executed);
  brt.mark_stable(last_stable);

  if (have_state)
  {
    // Re-authenticate my checkpoint message to mark it as stable or
    // if I do not have one put one in and make the corresponding
    // certificate complete.
    Checkpoint *c = elog.fetch(last_stable).mine();
    if (c == 0)
    {
      Digest d_state;
      state.digest(last_stable, d_state);
      c = new Checkpoint(last_stable, d_state, true);
      elog.fetch(last_stable).add_mine(c);
      elog.fetch(last_stable).make_complete();
    }
    else
    {
      c->re_authenticate(0, true);
    }

  }

  // Go over sset transfering any checkpoints that are now within
  // my window to elog.
  Seqno new_ls = last_stable;
  for (int i = 0; i < num_replicas; i++)
  {
    Checkpoint* c = sset.fetch(i);
    if (c != 0)
    {
      Seqno cn = c->seqno();
      if (cn < last_stable)
      {
        c = sset.remove(i);
        delete c;
        continue;
      }

      if (cn <= last_stable + max_out)
      {
        Certificate < Checkpoint > &cs = elog.fetch(cn);
        cs.add(sset.remove(i));
        if (cs.is_complete() && cn > new_ls)
          new_ls = cn;
      }
    }
  }

  //XXXXXXcheck if this is safe.
  if (new_ls > last_stable)
  {
    //    fprintf(stderr, "@@@@@@@@@@@@@@@               @@@@@@@@@@@@@@@               @@@@@@@@@@@@@@@\n");
    mark_stable(new_ls, elog.within_range(new_ls) && elog.fetch(new_ls).mine());
  }

  // Try to send any Pre_prepares for any buffered requests.
  if (primary() == id()) {
    send_pre_prepare();
  }
}

void PIR::handle(Data *m)
{
  //fprintf(stderr, "\t\t\t\t\t\tdata\n");
  //fprintf(stderr, "received data\n");
  state.handle(m);
}

void PIR::handle(Meta_data *m)
{
  //fprintf(stderr, "\t\t\t\t\t\tmetadata\n");

  state.handle(m);
}

void PIR::handle(Meta_data_d *m)
{
  //fprintf(stderr, "\t\t\t\t\t\tmetadatad\n");

  state.handle(m);
}

void PIR::handle(Fetch *m)
{
  //fprintf(stderr, "\t\t\t\t\t\tfetch\n");

  int mid = m->id();
  if (!state.handle(m, last_stable) && last_new_key != 0)
  {
#ifdef MSG_DEBUG
    fprintf(stderr, "Sending a last new key to all replicas\n");
#endif
    send(last_new_key, mid);
  }
}

void PIR::send_new_key()
{
  Node::send_new_key();
#ifdef MSG_DEBUG
  fprintf(stderr, " call to send new key\n");
#endif
  // Cleanup messages in incomplete certificates that are
  // authenticated with the old keys.
  int max = last_stable + max_out;
  int min = last_stable + 1;
  for (Seqno n = min; n <= max; n++)
  {
    if (n % checkpoint_interval == 0)
      elog.fetch(n).mark_stale();
  }

  if (last_executed > last_stable)
    min = last_executed + 1;

  for (Seqno n = min; n <= max; n++)
  {
    plog.fetch(n).mark_stale();
    clog.fetch(n).mark_stale();
  }

  vi.mark_stale();
  state.mark_stale();
}

void PIR::send_status(bool force)
{
  // Check how long ago we sent the last status message.
  Time cur = currentTime();
  if (force || diffTime(cur, last_status) > 100000)
  {
    //if(1) {
    // Only send new status message if last one was sent more
    // than 100 milliseconds ago.
    // [elwong] ... or if we are forcing it.
    last_status = cur;

    if (rr)
    {
      // Retransmit recovery request if I am waiting for one.
#ifdef MSG_DEBUG
      fprintf(stderr, "Sending a recovery request to all replicas\n");
#endif
      send(rr, All_replicas);
    }

    // If fetching state, resend last fetch message instead of status.
    if (state.retrans_fetch(cur))
    {
#ifdef MSG_DEBUG
    fprintf(stderr, "[PIR %i_%i] Sending a Fetch\n",
        protocol_instance_id, id());
#endif

      state.send_fetch(true);
      return;
    }

    Status s(v, last_stable, last_executed, has_new_view(),
        vi.has_nv_message(v));

    if (has_new_view())
    {
      // Set prepared and committed bitmaps correctly
      int max = last_stable + max_out;
      for (Seqno n = last_executed + 1; n <= max; n++)
      {
        Prepared_cert& pc = plog.fetch(n);
        if (pc.is_complete())
        {
          s.mark_prepared(n);
          if (clog.fetch(n).is_complete())
          {
            s.mark_committed(n);
          }
        }
        else
        {
          // Ask for missing big requests
          if (!pc.is_pp_complete() && pc.pre_prepare() && pc.num_correct()
              >= f())
            s.add_breqs(n, pc.missing_reqs());
        }
      }
    }
    else
    {
      vi.set_received_vcs(&s);
      vi.set_missing_pps(&s);
    }

    // Multicast status to all replicas.
    s.authenticate();
#ifdef MSG_DEBUG
    // there are too many status messages
    //fprintf(stderr, "Sending a status to all replicas\n");
#endif
    send(&s, All_replicas);
  }
}

void PIR::handle(Reply *m, bool mine)
{
  //  th_assert(false,"should not be there...\n");
  int mid = m->id();
  int mv = m->view();

#ifdef MSG_DEBUG
  fprintf(stderr,
      "Replica %i (primary=%i): handles a Reply of id %i and view %i\n",
      this->id(), primary(), mid, mv);
#endif

  if (rr && rr->request_id() == m->request_id() && (mine || !m->is_tentative()))
  {
    // Only accept recovery request replies that are not tentative.
    bool added = (mine) ? rr_reps.add_mine(m) : rr_reps.add(m);
    if (added)
    {
      if (rr_views[mid] < mv)
        rr_views[mid] = mv;

      if (rr_reps.is_complete())
      {
        // I have a valid reply to my outstanding recovery request.
        // Update recovery point
        int len;
        const char *rep = rr_reps.cvalue()->reply(len);
        th_assert(len == sizeof(Seqno), "Invalid message");

        Seqno rec_seqno;
        memcpy(&rec_seqno, rep, len);
        Seqno new_rp = rec_seqno / checkpoint_interval * checkpoint_interval
            + max_out;
        if (new_rp > recovery_point)
          recovery_point = new_rp;

        //  fprintf(stderr, "XXX Complete rec reply with seqno %qd rec_point=%qd\n",rec_seqno,  recovery_point);

        // Update view number
        //View rec_view = K_max<View>(f()+1, rr_views, n(), View_max);
        delete rr;
        rr = 0;
      }
    }
    return;
  }
  delete m;
}

void PIR::send_null()
{
  //fprintf(stderr, "@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@\n");
  th_assert(id() == primary(), "Invalid state");

  Seqno max_rec_point = max_out + (max_rec_n + checkpoint_interval - 1)
      / checkpoint_interval * checkpoint_interval;

  if (max_rec_n && max_rec_point > last_stable && has_new_view())
  {
    bool has_reqs; 
#ifdef LIMIT_REQ_LIST_USAGE
    if (protocol_instance_id == FAULTY_PROTOCOL_INSTANCE) {
      has_reqs = (rqueue.size() > 0);
    } else {
#endif
      has_reqs = !rlist->reqs_for_primary_empty();
#ifdef LIMIT_REQ_LIST_USAGE
    }
#endif

    if (!has_reqs && seqno <= last_executed && seqno + 1 <= max_out
        + last_stable)
    {
      // Send null request if there is a recovery in progress and there
      // are no outstanding requests.
      seqno++;
      Req_queue empty;
      Pre_prepare* pp = new Pre_prepare(view(), seqno, empty);
#ifdef MSG_DEBUG
      fprintf(stderr,
          "Replica %i (primary=%i): sending pre-prepare to all replicas\n",
          this->id(), primary());
#endif

      send(pp, All_replicas);
      plog.fetch(seqno).add_mine(pp);
    }
  }
  ntimer->restart();
  // TODO: backups should force view change if primary does not send null requests
  // to allow recoveries to complete.
}

//
// Timeout handlers:
//

void vtimer_handler()
{
  //RRBFT: this handler is deactivated
  return;

  //fprintf(stderr, " ###### @@ vtimer expired! @@ ######\n");

  th_assert(replica, "replica is not initialized\n");
  if (!replica->delay_vc())
  {
#ifdef MSG_DEBUG
    fprintf(
        stderr,
        "Replica %i, primary %i, view %qd : sending view change (vtimer expired)\n",
        replica->id(), replica->primary(), replica->view());
#endif
    replica->send_view_change();
  }
  else
  {
    //fprintf(stderr, "restarting vtimer 4\n");
    //fprintf(stderr, "vtimer restart (3)\n");
    replica->vtimer->restart();
  }
}

#ifdef THROUGHPUT_ADAPTIVE
//timer started after having observed a throughput lower than expected
void throughput_timer_handler()
{
  th_assert(replica, "replica is not initialized\n");
  if (!replica->delay_vc())
  {
    //    fprintf(stderr, "ADAPTIVE: sending view change for view %qd (throughput_timer expired)\n", replica->view()+1);
#ifdef DELAY_ADAPTIVE
    replica->pre_prepare_timer->stop();
#endif

    fprintf(
        stderr,
        "Replica %i, primary %i, view %qd : sending view change (throughput_timer expired)\n",
        replica->id(), replica->primary(), replica->view());
    replica->send_view_change();
  }
  else
  {
    if (replica->delay_vc())
    {
      //fprintf(stderr, "ADAPTIVE: restarting throughput_timer \n");
      replica->throughput_timer->restart();
    }
  }
}

void increment_timer_handler()
{
  th_assert(replica, "replica is not initialized\n");
  replica->time_to_increment = true;
  //  fprintf(stderr, "--- time to increment the required throughput! ---\n");
}
#endif

#ifdef DELAY_ADAPTIVE
void pre_prepare_timer_handler()
{
  th_assert(replica, "replica is not initialized\n");

  if (replica->id() != replica->primary() && replica->received_pre_prepares
      < expected_pre_prepares)
  {
    if (!replica->delay_vc())
    {
#ifdef THROUGHPUT_ADAPTIVE
      replica->throughput_timer->restop();
#endif

      fprintf(
          stderr,
          "ADAPTIVE: sending view change for view %qd (pre_prepare_timer expired). Doubling the period\n",
          replica->view() + 1);
      replica->pre_prepare_timer->adjust(
          replica->pre_prepare_timer->get_period() * 2);

      replica->send_view_change();

    }
    else
    {
      replica->pre_prepare_timer->restart();
    }
  }
  else
  {
    replica->pre_prepare_timer->restop();
    //    fprintf(stderr, "we passed the pre-prepare timer check, now we're resetting to the original value and restarting it\n");
    replica->pre_prepare_timer->adjust(pre_prepare_timer_duration);
    replica->pre_prepare_timer->restart();
    replica->received_pre_prepares = 0;
  }
}

#endif

void delay_pre_prepare_timer_handler()
{
  //replica->delay_pre_prepare_timer->stop();
  if (replica->has_new_view() && replica->primary() == replica->id()
      && replica->protocol_instance_id == FAULTY_PROTOCOL_INSTANCE) {
    //fprintf(stderr, "handler @ %qd usec\n", diffTime(rdtsc(), 0));
    replica->send_pre_prepare(true);
  }
}

void stimer_handler()
{
  //fprintf(stderr, "--- stimer expired --- \n");
  th_assert(replica, "replica is not initialized\n");

#ifdef MSG_DEBUG
  //fprintf(stderr, "[%s] Gonna send_status\n", __PRETTY_FUNCTION__);
#endif
  replica->send_status();
  replica->stimer->restart();
  //fprintf(stderr, "--- stimer restarted --- \n");
}

void ntimer_handler()
{
  //fprintf(stderr, " --- ntimer expired --- \n");
  th_assert(replica, "replica is not initialized\n");
  replica->send_null();
}

#ifdef DEBUG_PERIODICALLY_PRINT_THR
void pipes_thr_handler()
{
  Time now = currentTime();
  float elapsed_in_sec = diffTime(now, replica->pipes_thr_start) / 1000000.0;

  fprintf(stderr, "===== Pipes throughput handler %d =====\n", pipes_thr_handler_id++);
  fprintf(stderr, "*-* nb_received_requests=%f msg/sec\n", replica->nb_received_requests / elapsed_in_sec);
  fprintf(stderr, "*-* nb_sent_ordered_requests=%f msg/sec\n", replica->nb_sent_ordered_requests / elapsed_in_sec);
  for(int i=0; i<replica->n(); i++)
    fprintf(stderr, "*-* nb_sent_to_replicas[%d]=%f msg/sec\n", i, replica->nb_sent_to_replicas[i] / elapsed_in_sec);
  for(int i=0; i<replica->n(); i++)
    fprintf(stderr, "*-* nb_recv_from_replicas[%d]=%f msg/sec\n", i, replica->nb_recv_from_replicas[i] / elapsed_in_sec);
  fprintf(stderr, "==========\n");

  __sync_and_and_fetch(&replica->nb_received_requests, 0);
  __sync_and_and_fetch(&replica->nb_sent_ordered_requests, 0);
  for(int i=0; i<replica->n(); i++)
    for(int i=0; i<replica->n(); i++) __sync_and_and_fetch(&replica->nb_sent_to_replicas[i], 0);
  for(int i=0; i<replica->n(); i++)
    for(int i=0; i<replica->n(); i++) __sync_and_and_fetch(&replica->nb_recv_from_replicas[i], 0);
  replica->pipes_thr_start = currentTime();

  replica->pipes_thr_timer->restart();
}
#endif


void PIR::read_all_remaining_messages(void)
{
  fd_set file_descriptors; //set of file descriptors to listen to (only one socket, in this case)
  timeval listen_time; //max time to wait for something readable in the file descriptors

  fprintf(stderr, "========== read all remaining messages ==========\n");

  while (1) {
    FD_ZERO(&file_descriptors); //initialize file descriptor set

    FD_SET(verifier_to_pir_fd, &file_descriptors);
    int maxsock = verifier_to_pir_fd;

    listen_time.tv_sec = 0;
    listen_time.tv_usec = 500;

    int s = select(maxsock + 1, &file_descriptors, NULL, NULL, &listen_time);
    if (s <= 0) break;


    /* get a new connection from the verifier */
    if (FD_ISSET(verifier_to_pir_fd, &file_descriptors))
    {
      Message *m = new Message(Max_message_size);
      int ret = read(verifier_to_pir_fd, m->contents(), m->msize());

      if (ret >= (int) sizeof(Message_rep) && ret >= m->size()) {
        if (m->tag() == Request_tag)
        {
            Request *r = (Request*)m;
           fprintf(stderr, "PIR %i_%i has a remaining request: (%d, %qd)\n",
                protocol_instance_id, id(), r->client_id(), r->request_id());
        }
        else if (m->tag() == Protocol_instance_change_tag)
        {
             fprintf(stderr, "PIR %i_%i has a remaining Protocol instance change\n",
                protocol_instance_id, id());
        }
        else
        {
            fprintf(stderr, "PIR %i_%i has a remaining message whose tag is %i\n",
                protocol_instance_id, id(), m->tag());
        }
      }
      delete m;
    }
  }
}


static int stats_already_printed = 0;

void print_statistics_before_dying(int sig)
{
  if (!stats_already_printed)
  {
    stats_already_printed = 1;

    replica->read_all_remaining_messages();

    // requests in rqueue
    fprintf(stderr, "Requests in rqueue\ncid\trid\n");
    for (int i=0; i<replica->num_clients(); i++) {
      Request *r = replica->rqueue.first_client(i);
      if (r) {
        fprintf(stderr, "%d\t%qd\n", i, r->request_id());
      }
    }
    fprintf(stderr, "\n==============================\n\n");

    if (replica->id() != replica->primary()) {
      // pending pp and their requests
      fprintf(stderr, "Pending PP\n[Seqno]\tlist_of_<cid, rid>\n");
      struct pre_prepare_in_list *ppil;
      list_for_each_entry(ppil, &replica->pending_pp, link) {
        Pre_prepare *pp = ppil->pp;
        fprintf(stderr, "[%qd]", pp->seqno());

        Pre_prepare::Requests_iter iter(pp);
        Request req;

        while (iter.get(req))
        {
          fprintf(stderr, "\t<%d, %qd>", req.client_id(), req.request_id());
        }
        fprintf(stderr, "\n");
      }
      fprintf(stderr, "\n==============================\n\n");
    }

    // requests in rlist
    replica->rlist->print_min_ordered_array();
    replica->rlist->print_content();

    sleep(3600);
  }
}

bool PIR::has_req(int cid, const Digest &d)
{
  Request* req = rqueue.first_client(cid);

  if (req && req->digest() == d)
    return true;

  return false;
}

Message* PIR::pick_next_status()
{
  for (int identities_checked = 0; identities_checked < num_replicas; identities_checked++)
  {
    status_to_process++;
    status_to_process %= num_replicas;
    if (status_pending[status_to_process] != NULL)
    {
      //fprintf(stderr, "@@@@@@ handling stored status from %d @@@@@@ \n",status_to_process);
      Message* m = status_pending[status_to_process];
      status_pending[status_to_process] = NULL;
      return m;
    }
  }
  // status_pending is empty...
  return NULL;
}

void PIR::handle(View_change *m)
{
#ifdef MSG_DEBUG
  fprintf(
      stderr,
      "Replica %i  (primary=%i) (view %qd) handles a View change from %i for view %qd\n",
      this->id(), primary(), view(), m->id(), m->view());
#endif

  //muting replicas

  if (has_new_view() && m->view() > view())
  {
    // it seems that we get here only at the beginning of the execution.
    //fprintf(stderr, "Replica %i  (primary=%i) (view %qd): muted replica %d\n",
    //    this->id(), primary(), view(), m->id());

    excluded_replicas[m->id()] = true;
  }

#ifdef MSG_DEBUG
  int size_of_vc = sizeof(View_change_rep) + sizeof(Req_info) * m->rep().n_reqs;
   fprintf(stderr, 
   "Replica %i, primary %i, view %qd, checking received view change of size %i with seqno= %qd\n",
   id(), primary(), view(), size_of_vc, m->last_stable());
#endif

  /*
   char *toto = m->contents();
   for (int i = 0; i < size_of_vc / 4; i++)
   {
   fprintf(stderr, " %x", *(toto + (4 * i)));
   }
   fprintf(stderr, "--] END OF MESSAGE\n");
   */

  /*
   fprintf(
   stderr,
   "Replica %i, primary %i, view %qd, checking received view change of size %i with seqno= %qd from %i and msg view=%qd\n",
   id(), primary(), view(), size_of_vc, m->last_stable(), m->id(), m->view());
   char *toto = m->contents();
   for (int i = 0; i < m->size() / 4; i++)
   {
   fprintf(stderr, " %x", *(toto + (4 * i)));
   }
   fprintf(stderr, "--] END OF MESSAGE\n");
   */

  //  fprintf(stderr, "RECV: view change v=%qd from %d\n", m->view(), m->id());
  bool modified = vi.add(m);
  if (!modified)
  {
    //fprintf(stderr, "!modified\n");
    return;
  }
  // TODO: memoize maxv and avoid this computation if it cannot change i.e.
  // m->view() <= last maxv. This also holds for the next check.
  View maxv = vi.max_view();
  if (maxv > v)
  {
    // Replica has at least f+1 view-changes with a view number
    // greater than or equal to maxv: change to view maxv.
    v = maxv - 1;
    vc_recovering = true;
    //fprintf(stderr, "joining a view change\n");
    send_view_change();

    return;
  }

  if (limbo && primary() != node_id)
  {
    maxv = vi.max_maj_view();
    th_assert(maxv <= v, "Invalid state");

    if (maxv == v)
    {
      // Replica now has at least 2f+1 view-change messages with view  greater than
      // or equal to "v"

      // Start timer to ensure we move to another view if we do not
      // receive the new-view message for "v".
      //fprintf(stderr, "starting vtimer 1\n");
      //fprintf(stderr, "vtimer restart (6)\n");
      vtimer->restart();
      limbo = false;
      vc_recovering = true;
    }
  }
}

void PIR::handle(New_view *m)
{
  //fprintf(stderr, "\t\t\t\t\t\tnew view\n");
  //  fprintf(stderr, "RECV: new view v=%qd from %d\n", m->view(), m->id());

#ifdef MSG_DEBUG
  fprintf(
      stderr,
      "Replica %i (primary=%i) (view %qd) handles a New view from %d for view %qd\n",
      this->id(), primary(), view(), m->id(), m->view());
#endif

  vi.add(m);
}

void PIR::handle(View_change_ack *m)
{
  //fprintf(stderr, "RECV: view-change ack v=%qd from %d for %d\n", m->view(), m->id(), m->vc_id());

#ifdef MSG_DEBUG
  fprintf(
      stderr,
      "Replica %i (primary=%i) (view %qd) handles a View change ack from %i for %i for view %qd\n",
      this->id(), primary(), view(), m->id(), m->vc_id(), m->view());
#endif

  vi.add(m);
}

void PIR::send_view_change()
{
  // Do not send the view change if view changes are deactivated.
#ifdef VIEW_CHANGE_DEACTIVATED
  return;
#endif

  // Move to next view.

  //unmuting replicas
  for (int j = 0; j < num_replicas; j++)
    excluded_replicas[j] = false;

#ifdef MSG_DEBUG
  fprintf(stderr, "all replicas unmuted\n");
#endif

#ifdef DELAY_ADAPTIVE
  pre_prepare_timer->restop();
#endif
  v++;

#ifdef MSG_DEBUG
  fprintf(stderr, "sending a view_change, v: %qd, last_executed: %qd \n", v,
      last_executed);
#endif
  status_messages_badly_needed = true; // avoid status message de-prioritization

  //muting clients
  excluded_clients = true;

#ifdef THROUGHPUT_ADAPTIVE
  first_checkpoint_after_view_change = true;
  vc_already_triggered = false;
  throughput_timer->restop();
#endif

  cur_primary = v % num_replicas;
  limbo = true;
  vtimer->stop(); // stop timer if it is still running
  //fprintf(stderr, "stopping ntimer (2)\n");
  ntimer->restop();

  if (last_tentative_execute > last_executed)
  {
    // Rollback to last checkpoint
    th_assert(!state.in_fetch_state(), "Invalid state");
    Seqno rc = state.rollback();
    //    fprintf(stderr, "XXXRolled back in vc to %qd with last_executed=%qd\n", rc, last_executed);
    last_tentative_execute = last_executed = rc;
    //    fprintf(stderr, ":):):):):):):):) Set le = %d\n", last_executed);
  }

  last_prepared = last_executed;

  for (Seqno i = last_stable + 1; i <= last_stable + max_out; i++)
  {
    Prepared_cert &pc = plog.fetch(i);
    Certificate < Commit > &cc = clog.fetch(i);

    if (pc.is_complete())
    {
      vi.add_complete(pc.rem_pre_prepare());
    }
    else
    {
      Prepare *p = pc.my_prepare();
      if (p != 0)
      {
        vi.add_incomplete(i, p->digest());
      }
      else
      {
        Pre_prepare *pp = pc.my_pre_prepare();
        if (pp != 0)
        {
          vi.add_incomplete(i, pp->digest());
        }
      }
    }

    pc.clear();
    cc.clear();
    // TODO: Could remember info about committed requests for efficiency.
  }

  // Create and send view-change message.
  //  fprintf(stderr, "SND: view change %qd\n", v);
  vi.view_change(v, last_executed, &state);
}

void PIR::process_new_view(Seqno min, Digest d, Seqno max, Seqno ms)
{
  th_assert(ms >= 0 && ms <= min, "Invalid state");

#ifdef MSG_DEBUG
  fprintf(stderr,
      "XXX process new view: %qd\tmin: %qd\tmax: %qd\tms: %qd\ttime: %qd\n", v,
      min, max, ms, rdtsc());
#endif
  not_deprioritize_status_before_this = max + 1; //just to be sure... ;)

  // as we change the view, emtpy the list, because we will never
  // send these PP
  if (protocol_instance_id == FAULTY_PROTOCOL_INSTANCE && id() == 0 && byz_pre_prepare_delay) {
    struct delayed_pp *dpp;
    struct list_head *pos, *n;
    list_for_each_safe(pos, n, &delayed_pps) {
      dpp = list_entry(pos, struct delayed_pp, link);
      delete dpp->pp;
      list_del(pos);
      delete dpp;
    }
  }

  // empty the list of pending reqs if I am the new primary, as we do not use it
  if (primary() == id()) {
    rlist->empty_pending_reqs_list();
  }

#ifdef REPLICA_FLOOD_PROTECTION
  if (!flood_protection_active && view() > flood_protection_view
      + flood_protected_views)
  {
    flood_protection_active = true;
    //fprintf(stderr, " ******* FLOOD ******* UNMUTING!!! *****\n");
    for (int i = 0; i < num_replicas; i++)
    excluded_replicas[i] = false;
    //system("./unmute_nic eth5 &");
  }
  for (int i = 0; i < 5; i++)
  rmcount[i] = 0;

#endif

  vtimer->restop();
  limbo = false;
  vc_recovering = true;

  if (primary(v) == id())
  {
    New_view* nv = vi.my_new_view();
#ifdef MSG_DEBUG
    fprintf(
        stderr,
        "Replica %i (primary %i) in view %qd is sending a new view to all replicas\n",
        id(), primary(), view());
#endif
    send(nv, All_replicas);
  }

  // Setup variables used by mark_stable before calling it.
  seqno = max - 1;
  if (last_stable > min)
    min = last_stable;
  low_bound = min;

  if (ms > last_stable)
  {
    // Call mark_stable to ensure there is space for the pre-prepares
    // and prepares that are inserted in the log below.
    mark_stable(ms, last_executed >= ms);
  }

  // Update pre-prepare/prepare logs.
  th_assert(min >= last_stable && max - last_stable - 1 <= max_out,
      "Invalid state");
  for (Seqno i = min + 1; i < max; i++)
  {
    Digest d;
    Pre_prepare* pp = vi.fetch_request(i, d);
    Prepared_cert& pc = plog.fetch(i);

    if (primary() == id())
    {
      pc.add_mine(pp);
    }
    else
    {
      Prepare* p = new Prepare(v, i, d);
      pc.add_mine(p);
#ifdef MSG_DEBUG
      fprintf(
          stderr,
          "Replica %i, primary %i, view %qd: Sending a prepare to all replicas from process new view\n",
          id(), primary(), view());
#endif
      send(p, All_replicas);

      th_assert(pp != 0 && pp->digest() == p->digest(), "Invalid state");
      pc.add_old(pp);
    }
  }

  if (primary() == id())
  {
    send_pre_prepare();
    ntimer->start();
  }

  if (last_executed < min)
  {
    has_nv_state = false;
    state.start_fetch(last_executed, min, &d, min <= ms);
  }
  else
  {
    has_nv_state = true;

    // Execute any buffered read-only requests
    for (Request *m = ro_rqueue.remove(); m != 0; m = ro_rqueue.remove())
    {
      execute_read_only(m);
      delete m;
    }
  }

  if (primary() != id() && rqueue.size() > 0)
  {
    //fprintf(stderr, "vtimer restart (0)\n");
    vtimer->restart();
  }

#ifdef DELAY_ADAPTIVE
  pre_prepare_timer->restop();
  received_pre_prepares = 0;
  if (id() != primary())
  pre_prepare_timer->restart();
  //call_pre_prepare_timer->restart();
#endif

#ifdef THROUGHPUT_ADAPTIVE
  vc_already_triggered = false;
  time_to_increment = false;
  increment_timer->restart();
  checkpoints_in_new_view = 0;
  req_count_vc = 0;
  last_view_time = rdtsc();
#endif

  //unmuting clients
  excluded_clients = false;

  print_stuff = true;
  //fprintf(stderr, "DONE:process new view: %qd, has new view: %d\n", v, has_new_view());
}

