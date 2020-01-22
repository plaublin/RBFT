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
#include <pthread.h>

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
#include "Verifier.h"
#include "Statistics.h"
#include "State_defs.h"
#include "attacks.h"
#include "parameters.h"
#include "tcp_net.h"
#include "Verifier_thread.h"
#include "Execution_thread.h"
#include "Forwarding_thread.h"
#include "Circular_buffer.h"
#include "Protocol_instance_change.h"
#include "Propagate.h"

// Global replica object.
Verifier *replica;

// The Verifier_thread pointer.
Verifier_thread *verifier;
Execution_thread *executor;
Forwarding_thread *forwarding;

// Force template instantiation
#include "Certificate.t"
template class Certificate<Commit> ;
template class Certificate<Checkpoint> ;
template class Certificate<Reply> ;
//SONIA
template class Certificate<Request> ;

#include "Log.t"
template class Log<Prepared_cert> ;
template class Log<Certificate<Commit> > ;
template class Log<Certificate<Checkpoint> > ;
//SONIA
template class Log<Certificate<Request> > ;

#include "Set.t"
template class Set<Checkpoint> ;

template<class T>
void Verifier::retransmit(T *m, Time &cur, Time *tsent, Principal *p)
{
  // re-authenticate.
  m->re_authenticate(p);
  //    fprintf(stderr, "RET: %s to %d \n", m->stag(), p->pid());
  // Retransmit message
#ifdef MSG_DEBUG
  fprintf(stderr, "Replica is retransmitting to %i\n", p->pid());
#endif

  send(m, p->pid());
}


// Init the connection with the replicas
void Verifier::init_comm_with_replicas(void) {
  // =================================================================
  // Initialization PIRs <----> Verifier
  // =================================================================

  char chaname[256];

  pirs_to_verifier_fd = (int*) malloc(sizeof(*pirs_to_verifier_fd) * num_pirs());
  if (!pirs_to_verifier_fd)
  {
    perror("pirs_to_verifier_fd malloc");
    exit(-1);
  }

  snprintf(chaname, 256, "%s%i", KZIMP_CHAR_DEV_FILE, num_pirs());
  verifier_to_pirs_fd = open(chaname, O_WRONLY);
  //fprintf(stderr, "verifier_to_pirs_fd = (%i, %s)\n", verifier_to_pirs_fd, chaname);

  for (int i = 0; i < num_pirs(); i++)
  {
    snprintf(chaname, 256, "%s%i", KZIMP_CHAR_DEV_FILE, i);
    pirs_to_verifier_fd[i] = open(chaname, O_RDONLY);
    //fprintf(stderr, "pirs_to_verifier_fd[%i] = (%i, %s)\n", i, pirs_to_verifier_fd[i], chaname);
  }

  rr_last_recv = 0;
}

// Init the connection with the verifier
void Verifier::init_comm_with_verifier(void) {
#if USE_TCP_CONNECTIONS

  // =================================================================
  // Initialization Verifiers ----> Verifiers
  // =================================================================
  bootstrap_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (bootstrap_socket == -1)
  {
    perror("Error while creating the socket, exiting! ");
    exit(errno);
  }
  // TCP NO DELAY
  int flag = 1;
  int result = setsockopt(bootstrap_socket, IPPROTO_TCP, TCP_NODELAY,
      (char*) &flag, sizeof(int));
  if (result == -1)
  {
    perror("Error while setting TCP NO DELAY! ");
  }

  // 2) bind on it
  bootstrap_sin.sin_addr.s_addr = htonl(INADDR_ANY);
  bootstrap_sin.sin_family = AF_INET;
  bootstrap_sin.sin_port = htons(VERIFIER_BOOTSTRAP_PORT + id()); // we add id so that it is possible to launch the replicas on the same machine

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

  rcv_socks = (int*) malloc(sizeof(int) * num_replicas);
  snd_socks = (int*) malloc(sizeof(int) * num_replicas);
  for (int i = 0; i < num_replicas; i++)
  {
    rcv_socks[i] = -1;
    snd_socks[i] = -1;
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
    struct sockaddr_in addr;
    //struct hostent *hp = gethostbyname(replicas_hostname[i]);
    //bcopy((char *) hp->h_addr, (char *) &addr.sin_addr.s_addr, hp->h_length);

    // change the IP address, so that the forwarder thread uses another NIC
    unsigned int s_addr = inet_addr(replicas_ipaddr[i]);
    if (f() == 1) {
        NIPQUADi(s_addr, 2) = NETWORK_INTERFACE_IP_FOR_FORWARDER;
    }

    addr.sin_addr.s_addr = s_addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(VERIFIER_BOOTSTRAP_PORT + i); // we add i so that it is possible to launch the replicas on the same machine

    fprintf(stderr, "Node %d connects to verifier %d at %3d.%3d.%3d.%3d:%i\n",
       id(), i, NIPQUAD(s_addr),ntohs(addr.sin_port)) ;

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
        fprintf(stderr, "Connection successful from verifiers %i to %i!\n",
            id(), i);
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

    //char *hostname = inet_ntoa(csin.sin_addr);

    int id_of_rcv = 0;
    unsigned int s_addr;
    while (id_of_rcv < num_replicas)
    {
      // change the IP address, so that the forwarder thread uses another NIC
      s_addr = inet_addr(replicas_ipaddr[id_of_rcv]);
      if (f() == 1) {
          NIPQUADi(s_addr, 2) = NETWORK_INTERFACE_IP_FOR_FORWARDER;
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
    fprintf(
        stderr,
        "A connection has been accepted from verifiers %3d.%3d.%3d.%3d:%i, id=%i\n",
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

  // =================================================================
  // Initialization Verifier <----> Verifier --- LOAD BALANCING
  // =================================================================

  rcv_socks_lb = (int*) malloc(sizeof(int) * num_replicas);
  snd_socks_lb = (int*) malloc(sizeof(int) * num_replicas);
  for (int i = 0; i < num_replicas; i++)
  {
    rcv_socks_lb[i] = -1;
    snd_socks_lb[i] = -1;
  }

  for (int i = 0; i <= id(); i++)
    {
      // 1) create the socket
      snd_socks_lb[i] = socket(AF_INET, SOCK_STREAM, 0);
      if (snd_socks_lb[i] == -1)
      {
        perror("Error while creating the socket (load balancing)! ");
        exit(errno);
      }

      // 2)  connect to the server
      // since we have multiple NICs, use replicas_ipaddr[], and not replicas_hostname[]
      struct sockaddr_in addr;
      //struct hostent *hp = gethostbyname(replicas_hostname[i]);
      //bcopy((char *) hp->h_addr, (char *) &addr.sin_addr.s_addr, hp->h_length);

      // change the IP address, so that the forwarder thread uses another NIC
      unsigned int s_addr = inet_addr(replicas_ipaddr[i]);
      if (f() == 1) {
          NIPQUADi(s_addr, 2) = get_interface_between_i_and_j(1, id(), i);
      }

      addr.sin_addr.s_addr = s_addr;
      addr.sin_family = AF_INET;
      addr.sin_port = htons(VERIFIER_BOOTSTRAP_PORT + i); // we add i so that it is possible to launch the replicas on the same machine

      //fprintf(stderr, "Node %d connects to verifier %d at %3d.%3d.%3d.%3d:%i\n",
      //   id(), i, NIPQUAD(s_addr),ntohs(addr.sin_port)) ;

      // 2b) TCP NO DELAY
      flag = 1;
      result = setsockopt(snd_socks_lb[i], IPPROTO_TCP, TCP_NODELAY, (char *) &flag,
          sizeof(int));

      if (result == -1)
      {
        perror("Error while setting TCP NO DELAY (load balancing)! ");
        exit(-1);
      }

      // 2c) connect
      while (true)
      {
        if (connect(snd_socks_lb[i], (struct sockaddr *) &(addr), sizeof(addr)) < 0)
        {
          perror("Cannot connect, attempting again (load balancing)..");
          sleep(1);
        }
        else
        {
          fprintf(stderr, "Connection successful from verifiers %i to %i (load balancing)!\n",
              id(), i);
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
        perror("An invalid socket has been accepted (load balancing)! ");
        continue;
      }

      // TCP NO DELAY
      flag = 1;
      result = setsockopt(rcv_sock, IPPROTO_TCP, TCP_NODELAY, (char *) &flag,
          sizeof(int));
      if (result == -1)
      {
        perror("Error while setting TCP NO DELAY (load balancing)! ");
      }

      //char *hostname = inet_ntoa(csin.sin_addr);

      int id_of_rcv = 0;
      unsigned int s_addr;
      while (id_of_rcv < num_replicas)
      {
        // change the IP address, so that the forwarder thread uses another NIC
        s_addr = inet_addr(replicas_ipaddr[id_of_rcv]);
        if (f() == 1) {
            NIPQUADi(s_addr, 2) = get_interface_between_i_and_j(1, id(), id_of_rcv);
        }


        if (csin.sin_addr.s_addr == s_addr)
        {
          break;
        }
        id_of_rcv++;
      }

      if (id_of_rcv >= num_replicas)
      {
        fprintf(stderr, "Unknown host: %3d.%3d.%3d.%3d (load balancing)\n",
            NIPQUAD(csin.sin_addr.s_addr));
      }

      rcv_socks_lb[id_of_rcv] = rcv_sock;

      // print some information about the accepted connection
      fprintf(
          stderr,
          "A connection has been accepted from verifiers %3d.%3d.%3d.%3d:%i, id=%i (load balancing)\n",
          NIPQUAD(s_addr),ntohs(csin.sin_port), id_of_rcv) ;
    }

  for (int i = 0; i < num_replicas; i++)
  {
    if (rcv_socks_lb[i] == -1)
    {
      rcv_socks_lb[i] = snd_socks_lb[i];
    }
    if (snd_socks_lb[i] == -1)
    {
      snd_socks_lb[i] = rcv_socks_lb[i];
    }
  }

#else

   snd_socks = NULL;
   rcv_socks = (int*) malloc(sizeof(int) * num_replicas);

   // for each replica i
   //   create socket on port VERIFIER_BOOTSTRAP_PORT + i
   //   bind on it (it will be for comm from replica i)
   for (int i = 0; i < num_replicas; i++)
   {
     // initializing
     rcv_socks[i] = socket(AF_INET, SOCK_DGRAM, 0);

     fprintf(stderr, "creating socket %d for comm with verifier %d\n", rcv_socks[i], i);

     Addr tmp;
     tmp.sin_family = AF_INET;
     tmp.sin_port = htons(VERIFIER_BOOTSTRAP_PORT + i);
     tmp.sin_addr.s_addr = inet_addr(replicas_ipaddr[id()]);
     if (f() == 1) {
         NIPQUADi(tmp.sin_addr.s_addr, 2) = NETWORK_INTERFACE_IP_FOR_FORWARDER;
     } else {
       in_addr_t s_addr = principals[i]->address()->sin_addr.s_addr;
       NIPQUADi(tmp.sin_addr.s_addr, 2) = NIPQUADi(s_addr, 2);
     }

     fprintf(stderr,
         "Verifier %d binds on %d.%d.%d.%d:%i for comm with replica %d\n",
         id(), NIPQUAD(tmp.sin_addr.s_addr), ntohs(tmp.sin_port), i);

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
     principals[i]->updateAddrPort(VERIFIER_BOOTSTRAP_PORT + id());
     in_addr_t s_addr = principals[i]->address()->sin_addr.s_addr;
     if (f() == 1) {
         NIPQUADi(s_addr, 2) = NETWORK_INTERFACE_IP_FOR_FORWARDER;
         principals[i]->updateAddrIP(s_addr);
     }

     const Addr *to = principals[i]->address();
     fprintf(stderr,
         "Verifier %d can send a message to node %d via %d.%d.%d.%d:%i\n",
         id(), i,
         NIPQUAD(to->sin_addr.s_addr),ntohs(to->sin_port));
   }

#ifdef LOAD_BALANCING_SEND
#undef LOAD_BALANCING_SEND
   fprintf(stderr, "!!! WARNING: DEACTIVATING LOAD BALANCING AT THE FORWARDING THREAD WITH UDP !!!\n");

#endif

   snd_socks_lb = NULL;
   rcv_socks_lb = (int*) malloc(sizeof(int) * num_replicas);
   for (int i = 0; i < num_replicas; i++)
   {
     rcv_socks_lb[i] = -1;
   }

#endif
}

Verifier::Verifier(FILE *config_file, FILE *config_priv, char *mem, int nbytes,
    int _byz_pre_prepare_delay, bool _small_batches,
    long int _exec_command_delay) :
  Node(config_file, config_priv), replies(mem, nbytes, num_principals), rqueue(), ro_rqueue(),
    plog(max_out), clog(max_out), elog(max_out * 2, 0), 
    sset(n()), state(this, mem, nbytes), vi(node_id, 0)
{
  // Fail if node is not a replica.
  if (!is_replica(id()))
    th_fail("Node is not a replica");

  // The following two lines implement the 'slow' primary
  byz_pre_prepare_delay = _byz_pre_prepare_delay;
  small_batches = _small_batches;
  exec_command_delay = _exec_command_delay;

#ifdef FAIRNESS_ADAPTIVE
  max_rqueue_size = 0;
  executed_snapshot = (Seqno*) malloc(num_principals * sizeof(Seqno));
  for (int j = 0; j < num_principals; j++)
  executed_snapshot[j] = -1;
  fairness_bound = fairness_multiplier * num_principals;
#endif

#ifdef THROUGHPUT_ADAPTIVE
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

  first_request_time = 0;

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

  // Create timers and randomize times to avoid collisions.
  srand48(getpid());

  delay_pre_prepare_timer = new ITimer(byz_pre_prepare_delay,
      delay_pre_prepare_timer_handler);

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

  // Timer deactivated in the Verifier
  //vtimer = new ITimer(vt + lrand48() % 100, vtimer_handler);
  //stimer = new ITimer(st + lrand48() % 100, stimer_handler);
  //stimer->start(); //not started in oldbft, so not starting here

#ifdef PERIODICALLY_MEASURE_THROUGHPUT
  periodic_thr_measure = new ITimer(PRINT_THROUGHPUT_PERIOD,
      periodic_thr_measure_handler);
  nb_requests_4_periodic_thr_measure = 0;
  next_measure_idx = 0;
  start_cycle_4_periodic_thr_measure = currentTime();
  periodic_thr_measure->start();
#endif

  if (!floodMax())
  {
    statTimer = new ITimer(MONITORING_PERIOD, statTimer_handler);
  }

  statTimer_first_call = 0;

#ifdef DEBUG_PERIODICALLY_PRINT_THR
  pipes_thr_timer = new ITimer(DEBUG_PERIODIC_THR_PERIOD, pipes_thr_handler);
  pipes_thr_start = currentTime();
  nb_received_requests = 0;
  nb_sent_requests_to_forwarder = 0;
  nb_recv_requests_from_verifier_thr = 0;
  nb_sent_propagate = 0;
  nb_recv_propagate = new int[n()]; // 1 value for each node
  for(int i=0; i<n(); i++) nb_recv_propagate[i] = 0;
  nb_sent_requests_to_verifier = 0;
  nb_recv_requests_from_forwarder = 0;
  nb_sent_requests_to_replicas = 0;
  nb_recv_requests_from_replicas = new int[num_pirs()]; // 1 value for each replica
  for(int i=0; i<num_pirs(); i++) nb_recv_requests_from_replicas[i] = 0;
  nb_sent_requests_to_exec = 0;
  nb_recv_requests_from_verifier = 0;
  nb_sent_replies = 0;
#endif

  statsComputer = new Verifier_stats(num_replicas, num_clients(), num_pirs());
  //nb_monitoring_since_last_pic = MONITORING_GRACE_PERIOD;
  nb_monitoring_since_last_pic = 0;

  nb_retransmissions = 0;
  nb_executed = 0;

  send_to_replica_start = currentTime();
  recv_ordered_req_start = currentTime();
  nb_sent_to_replica_req = 0;
  nb_recv_ordered_req = 0;

#ifdef SAVE_REQUESTS_TIMES
  next_request_idx_toe = 0;
  next_request_idx_toa = 0;
#endif

  signal(SIGINT, print_statistics_before_dying);

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

  //Initialize the certificates
  certificates = new std::unordered_map<Request_id, forwarding_cert>[num_clients()];

  /*************** protocol instance change stuff ***************/
  pic_quorum.bitmap = new Bitmap(num_replicas, false);
  pic_quorum.nb_msg = 0;
  pic_quorum.protocol_instance_id = 1;
  current_protocol_instance = 1;
  /**************************************************************/

  print_stuff = true;

  //PL: this is better to close the file descriptor when it is no longer used
  fclose(config_file);

  excluded_clients = false;

  excluded_replicas = (bool*) malloc(num_replicas * sizeof(bool));

  fprintf(stderr, "I am replica %d\n", id());

  for (int j = 0; j < num_replicas; j++)
  {
    //resetting the excluded_replicas array
    excluded_replicas[j] = false;
  }

  status_pending = (Message**) malloc(num_replicas * sizeof(Message*));
  for (int index = 0; index < num_replicas; index++)
    status_pending[index] = NULL;
  status_to_process = 0;
  s_identity = -1;

  //blacklist malloc and initialization
  blacklisted = (bool*) malloc(sizeof(bool) * num_principals);
  for (int i = 0; i < num_principals; i++)
    blacklisted[i] = false;

  init_comm_with_verifier();

  init_comm_with_replicas();

  verifier_to_executor_buffer = new Blocking_circular_buffer(
      circular_buffer_size, (char*)"verifier_to_exec_thr");
  verifier_thr_to_fwd_thr_buffer = new Circular_buffer(circular_buffer_size, (char*)"Verifier_thr_to_fwd_thr");
  request_buffer = new Circular_buffer(circular_buffer_size, (char*)"request_buffer");

  min_exec_rid = new Request_id[num_clients()];
  last_replies = new std::map<Request_id, Reply*>[num_clients()];
  certificates_lock = new pthread_mutex_t[num_clients()];
  missing_reqs_lock = new pthread_mutex_t[num_clients()];
  for (int i = 0; i < num_clients(); i++)
  {
    min_exec_rid[i] = 0;
    pthread_mutex_init(&certificates_lock[i], NULL);
    pthread_mutex_init(&missing_reqs_lock[i], NULL);
  }

  pthread_t verifier_thread;
  int rc;
  verifier = new Verifier_thread();
  rc
      = pthread_create(&verifier_thread, NULL, Verifier_thread_startup,
          verifier);
  if (rc)
  {
    fprintf(stderr, "Failed to create the verifier thread\n");
    exit(1);
  }
  else
  {
    fprintf(stderr, "Launched the verifier thread\n");

  }

  pthread_t executor_thread;
  executor = new Execution_thread();
  rc = pthread_create(&executor_thread, NULL, Execution_thread_startup,
      executor);
  if (rc)
  {
    fprintf(stderr, "Failed to create the executor thread\n");
    exit(1);
  }
  else
  {
    fprintf(stderr, "Launched the executor thread\n");
  }

  pthread_t forwarding_thread;
  forwarding = new Forwarding_thread();
  rc = pthread_create(&forwarding_thread, NULL, Forwarding_thread_startup,
      forwarding);
  if (rc)
  {
    fprintf(stderr, "Failed to create the forwarding thread\n");
    exit(1);
  }
  else
  {
    fprintf(stderr, "Launched the forwarding thread\n");
  }

  cpu_set_t cpuset;

  // associate this thread to CPU 0
  CPU_ZERO(&cpuset);
  CPU_SET(0, &cpuset);

  int s = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
  if (s != 0)
  {
    fprintf(stderr, "Error while associating this thread to CPU 0\n");
  }

  // associate the verifier thread to CPU 1
  CPU_ZERO(&cpuset);
  CPU_SET(1, &cpuset);
  s = pthread_setaffinity_np(verifier_thread, sizeof(cpu_set_t), &cpuset);
  if (s != 0)
  {
    fprintf(stderr, "Error while associating Verifier thread to CPU 1\n");
  }

  // associate the executor thread to CPU 2
  CPU_ZERO(&cpuset);
  CPU_SET(2, &cpuset);
  s = pthread_setaffinity_np(executor_thread, sizeof(cpu_set_t), &cpuset);
  if (s != 0)
  {
    fprintf(stderr, "Error while associating Executor thread to CPU 2\n");
  }

  // associate the forwarding thread to CPU 3
  CPU_ZERO(&cpuset);
  CPU_SET(3, &cpuset);
  s = pthread_setaffinity_np(forwarding_thread, sizeof(cpu_set_t), &cpuset);
  if (s != 0)
  {
    fprintf(stderr, "Error while associating Forwarding thread to CPU 3\n");
  }

  // measure cost of verifying signatures, MACs, and generating MACs
  statsComputer->measure_mac_sig_costs();

  fprintf(stderr, "********** Verifier is ready **********\n");
}

void Verifier::register_exec(
    int(*e)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool, long int))
{ //last long int is the execute delay
  exec_command = e;
}

void Verifier::register_nondet_choices(void(*n)(Seqno, Byz_buffer *),
    int max_len)
{

  non_det_choices = n;
  max_nondet_choice_len = max_len;
}

void Verifier::compute_non_det(Seqno s, char *b, int *b_len)
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

Verifier::~Verifier()
{
}

/**
 * This is the method executed in loop by the Verifier.
 */
void Verifier::recv()
{
  // Compute session keys and send initial new-key message.
  Node::send_new_key();

  // Compute digest of initial state and first checkpoint.
  state.compute_full_digest();

  // Start status and authentication freshness timers
  //stimer->start();
  //atimer->start();
  //if (id() == primary())
  //  ntimer->start();

  if (!floodMax())
  {
    statTimer->start();
  }

#ifdef DEBUG_PERIODICALLY_PRINT_THR
  pipes_thr_timer->start();
#endif

  // Allow recoveries
  rec_ready = true;

  fprintf(stderr, "Verifier ready: starting method recv()\n");

  //int count_idle = 0;
  //int max_queue_size=0;
  //int silly_counter=1;

  long int client_count = 0;

  //long nb_recv_replies = 0;
  Message* m;
  Message* mp;

  fd_set file_descriptors; //set of file descriptors to listen to (only one socket, in this case)
  timeval listen_time; //max time to wait for something readable in the file descriptors

  nb_batch_sent = 0;
  average_batch_size = 0.0;
  sum_batch_size = 0;

  while (1)
  {
    FD_ZERO(&file_descriptors); //initialize file descriptor set

    FD_SET(pirs_to_verifier_fd[0], &file_descriptors);
    int maxsock = pirs_to_verifier_fd[0];

    for (int j = 1; j < num_pirs(); j++)
    {
      FD_SET(pirs_to_verifier_fd[j], &file_descriptors);
      maxsock = MAX(maxsock, pirs_to_verifier_fd[j]);
    }

    FD_SET(request_buffer->fd,&file_descriptors);
    maxsock = MAX(maxsock, request_buffer->fd);

    listen_time.tv_sec = 0;
    listen_time.tv_usec = 500;

    bool idle = true;
    int ret = select(maxsock + 1, &file_descriptors, NULL, NULL, &listen_time);

    ITimer::handle_timeouts();

    if (ret <= 0) {
        continue; //nothing to read
    }

    /*****************************/
    if (FD_ISSET(request_buffer->fd, &file_descriptors))
    {
      m = request_buffer->cb_read_msg();
#ifdef MSG_DEBUG
      fprintf(stderr, "Verifier: read message @%p in circular buffer\n", m);
#endif

      if (m && m != request_buffer->cb_magic())
      {
        if (m->tag() == Request_tag)
        {
#ifdef DEBUG_PERIODICALLY_PRINT_THR
          __sync_fetch_and_add (&replica->nb_recv_requests_from_forwarder, 1);
#endif
          handle((Request*) m);
        }
        else if (m->tag() == Protocol_instance_change_tag)
        {
          handle((Protocol_instance_change*) m);
          delete m;
        }
        else if (m->tag() == New_key_tag)
        {
#ifdef MSG_DEBUG
          fprintf(stderr, "Replica %i, View %qd, received a New_key msg.\n", id(),
              ((Node*) this)->view());
#endif
          ((New_key*) m)->verify();
          delete m;
        }
        else
        {
#ifdef MSG_DEBUG
          fprintf(stderr,
              "Replica %i received a message with tag %i from a client\n",
              id(), m->tag());
#endif

          delete m;
        }
      }
    }

    // ========================================================================
    // Handle messages from PIRs in a round-robin manner
    // ========================================================================
    //fprintf(stderr, "Received something from PIR ");

    for (int x = 0; x < num_pirs(); x++)
    {
      //I want to listen at this PIR
      //j is the index of the current PIR to access in a round-robin manner
      int j = (rr_last_recv + x) % num_pirs();
      if (FD_ISSET(pirs_to_verifier_fd[j], &file_descriptors))
      {
        //replica->something_to_read++;
        client_count = 0;
        idle = false;

        // fprintf(stderr, " %d ", j);

        mp = new Message(Max_message_size);

        int ret = read(pirs_to_verifier_fd[j], mp->contents(), mp->msize());

        if (ret >= (int) sizeof(Message_rep) && ret >= mp->size())
        {
          if (mp->tag() == Wrapped_request_tag)
          {
            handle((Wrapped_request*) mp, j);
          }
          else
          {
            /*fprintf(stderr,
             "Verifier %i received a message with tag %i from PIR %i\n",
             id(), m->tag(), j);*/
            delete mp;
          }
        }
        else
        {
          delete mp;
          // fprintf(stderr, "--------- WTF? --------- ret: %d from replica %d\n",ret,j);
          if (ret < 0)
            fprintf(stderr, "errno: %s\n", strerror(errno));
        }
      } // end FD_ISSET
      /*else{
       replica->nothing_to_read++;
       }*/
    } // end for each pir
    //fprintf(stderr, "\n");
    if (num_pirs() > 1)
      rr_last_recv = (rr_last_recv + 1) % num_pirs();

  } // end while(1)
}

void Verifier::send(Message *m, int i)
{
  th_assert(i == All_replicas || i >= 0, "Invalid argument");
  m->check_msg();

  // send to all the PIRs
  if (i == All_replicas)
  {
    /*
     for (int x = 0; x < num_pirs(); x++)
     {
     #ifdef MSG_DEBUG
     //fprintf(stderr, "\tVerifier is sending a message to pir %i (x=%i) (rr_verifier_last=%i)\n",
     //    (rr_verifier_last + x) % num_pirs(), x, rr_verifier_last);
     #endif
     //sendMsg(pirs_sockets_fds[(rr_verifier_last + x) % num_pirs()],m->contents(), m->size());
     sendMsg(pirs_sockets_fds[x],m->contents(), m->size());
     }
     if(num_pirs() > 1)
     rr_verifier_last = (rr_verifier_last + 1) % num_pirs();
     */
    write(verifier_to_pirs_fd, m->contents(), m->size());
  }
  else if (i < num_replicas)
  {
    //sendMsg(pirs_sockets_fds[i], m->contents(), m->size());
    fprintf(stderr, "Sending to only one PIR should never happen: %i !!!\n", i);
    exit(-1);
  }
  else
  {
#if USE_TCP_CONNECTIONS
    sendMsg(clients_sockets_fds[i - num_replicas], m->contents(), m->size());
#else
    sendUDP(m, i);
#endif
  }
}

// verify a request comming from a Client
Request* Verifier::verify(Wrapped_request* wrapped)
{
  if (blacklisted[wrapped->client_id()])
  {
    fprintf(stderr, "[Replica %i] client %i is blacklisted\n", replica->id(),
        wrapped->client_id());

    delete wrapped;
    return NULL;
  }

  bool verified_wrap = wrapped->verify_MAC();
  //SONIA: THIS IS AN UGLY HACK
  verified_wrap = Message::is_mac_valid((Message_rep*) (wrapped->contents()));

  if (!verified_wrap)
  {
#ifdef MSG_DEBUG
    fprintf(stderr, "[Replica %i] request %qd, mac from client %i is not valid, deleting wrapped\n",
        replica->id(), wrapped->seqno(), wrapped->client_id());
#endif

    delete wrapped;
    return NULL;
  }
  else
  {
#ifdef MSG_DEBUG
//    fprintf(stderr, "[Replica %i] request %qd, mac from client %i is valid\n",
//        replica->id(), wrapped->seqno(), wrapped->client_id());
#endif
  }

  // verify_MAC returned true, so digest and MAC are ok.
  // now we call verify_request to verify the signature.

  verified_wrap = wrapped->verify_request();
  
  //PL: big hack. we have a problem with MACs/signatures on the terminators.
  // Sometimes a client get blacklisted here, while it is a correct client...
  verified_wrap = Message::is_mac_valid((Message_rep*) (wrapped->contents()));
  if (!verified_wrap)
  {
    // adding the sender of this wrapped request to the blacklist
    blacklisted[wrapped->client_id()] = true;
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

void Verifier::jump_table(Message* m)
{
  // All messages are handled in the recv() method.
  fprintf(stderr, "Not supposed to be called, exiting.\n");
  exit(-1);
}

void Verifier::send_request_to_PIRs(Request *req)
{
  seqno++;
#ifdef DEBUG_PERIODICALLY_PRINT_THR
          __sync_fetch_and_add (&replica->nb_sent_requests_to_replicas, 1);
#endif

  int old_size = req->msize();
  req->set_size_wo_padding(sizeof(Request_rep));
  send(req, All_replicas);
  req->set_size_wo_padding(old_size);
}

void Verifier::handle(Request *m)
{
  int cid = m->client_id();
  bool ro = m->is_read_only();
  Request_id rid = m->request_id();

  // Replica's requests must be signed and cannot be read-only.
  if (is_replica(cid) && (!m->is_signed() || ro))
  {
    return;
  }

  if (ro)
  {
    fprintf(stderr, "[Verifier] request %qu from client %i is RO...\n", rid, cid);

    // Read-only requests.
    if (execute_read_only(m) || !ro_rqueue.append(m)) {
    }
    return;
  }

#ifdef SAVE_REQUESTS_TIMES
    if (next_request_idx_toa < MAX_NB_REQUESTS)
    {
      times_of_arrival[next_request_idx_toa].cid = cid;
      times_of_arrival[next_request_idx_toa].rid = rid;
      times_of_arrival[next_request_idx_toa].time = currentTime();
      next_request_idx_toa++;
    }
#endif

#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i, Primary=%i, request %qu from client %i: Sending request to the PIRs, tag is: %i\n", id(), primary(), rid, cid, m->tag());
#endif

    send_request_to_PIRs(m);

    // add request in the verifierStat structure for computing stats
    statsComputer->add_request_for_latency(cid, rid);
}

void Verifier::handle(Wrapped_request *m, int pir_instance_id)
{
  // a wrapped request is sent by the PIR for execution
#ifdef MSG_DEBUG
  if (pir_instance_id == 0) {
    fprintf(stderr, "Handles a Wrapped request %qd with %d requests from PIR %d (check: should be %d)\n", m->seqno(), m->client_id(), m->extra(), pir_instance_id);
  }
#endif

#ifdef DEBUG_PERIODICALLY_PRINT_THR
  __sync_fetch_and_add (&replica->nb_recv_requests_from_replicas[pir_instance_id], m->client_id());
#endif

  // view contains the number of requests of this wrapped request
  statsComputer->add_request_for_throughput(pir_instance_id, m->client_id());

  Wrapped_request::Requests_iter iterator(m);
  Request req;
  Time recv_time = currentTime();
  while (iterator.get(req))
  {
    //fprintf(stderr, "Request %qd from %d in wrapped request %qd\n", req.request_id(), req.client_id(), m->seqno());
    statsComputer->add_reply_for_latency(recv_time, req.client_id(), req.request_id(),
        pir_instance_id);
  }

  if (pir_instance_id == 0)
  {
//    fprintf(stderr, "{%.4f} Handles a Wrapped request with %d requests from PIR %d\n", (float)diffTime(currentTime(), 0) / 1000000.0, m->client_id(), m->extra());
#ifdef DEBUG_PERIODICALLY_PRINT_THR
  __sync_fetch_and_add (&replica->nb_sent_requests_to_exec, m->client_id());
#endif
    replica->verifier_to_executor_buffer->bcb_write_msg((Message*) m);
 //   fprintf(stderr, "{%.4f} Message writtent\n", (float)diffTime(currentTime(), 0) / 1000000.0);
  } else {
    delete m;
  }

  if (nb_monitoring_since_last_pic > MONITORING_GRACE_PERIOD && statsComputer->latency_not_acceptable())
  {
#ifdef MSG_DEBUG
    fprintf(stderr, "The latency difference is too high. Sending a Protocol_instance_change\n",);
#endif
#ifndef PROTOCOL_INSTANCE_CHANGE_DEACTIVATED
    create_and_send_protocol_instance_change();
#endif
  }
}

void Verifier::send_pre_prepare(bool force_send)
{
  if (!has_new_view())
  {
    //    fprintf(stderr, "Should not be here!!!!!\n");
    return;
  }
  th_assert(primary() == id(), "Non-primary called send_pre_prepare");

  // if the parameter byz_pre_prepare_delay is not 0, this
  // is a byzantine replica that waits a delay of
  // byz_pre_prepare_delay us before sending the pre_prepare
  if (byz_pre_prepare_delay && !force_send)
  {
    delay_pre_prepare_timer->restart();
    return;
  }

  // If rqueue is empty there are no requests for which to send
  // pre_prepare and a pre-prepare cannot be sent if the seqno excedes
  // the maximum window or the replica does not have the new view.
  if ((force_send || rqueue.size() > 0) && seqno + 1 <= last_executed
      + congestion_window && seqno + 1 <= max_out + last_stable
      && has_new_view())
  {
    //  fprintf(stderr, "requeu.size = %d\n", rqueue.size());
    //  if (seqno % checkpoint_interval == 0)
    //if (print_stuff)
    //  fprintf(stderr, "SND: PRE-PREPARE seqno= %qd last_stable= %qd\n", seqno+1, last_stable);

    long old_size = rqueue.size();
    if (nbreqs >= 10000)
    {
#ifdef MSG_DEBUG
      fprintf(stderr, "Avg batch sz: %f\n", (float) nbreqs / (float) nbrounds);
#endif

      nbreqs = nbrounds = 0;
    }
    // Create new pre_prepare message for set of requests
    // in rqueue, log message and multicast the pre_prepare.
    seqno++;
    //    fprintf(stderr, "Sending PP seqno %qd\n", seqno);

    Pre_prepare *pp = new Pre_prepare(view(), seqno, rqueue);
    nbreqs += old_size - rqueue.size();
    nbrounds++;
    // TODO: should make code match my proof with request removed
    // only when executed rather than removing them from rqueue when the
    // pre-prepare is constructed.

    //if (print_stuff)
    //fprintf(stderr, "SND:  pp: (%qd, %qd),  last stable: %qd\n",seqno, pp->view(),  last_stable);
#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i, Primary=%i: Sending a pre prepare with seqno= %qd to all replicas\n", id(), primary(), pp->seqno());
#endif
    send(pp, All_replicas);
    plog.fetch(seqno).add_mine(pp);
    //    vtimer->stop(); //not in old pbft, so commented here
  }
  else
  {
    /*
     fprintf(stderr, "Replica %i, Primary=%i: pre prepare %qd has not been sent\n", id(),
     primary(), seqno);
     */

    /*
     printf(
     "force_send=%s, rqueue.size()=%i, seqno=%qd, last_executed=%qd, congestion_window=%i, max_out=%i, last_stable=%qd, has_new_view()=%s\n",
     (force_send) ? "true" : "false", rqueue.size(), seqno, last_executed,
     congestion_window, max_out, last_stable, (has_new_view()) ? "true"
     : "false");
     */

  }

}

template<class T>
bool Verifier::in_w(T *m)
{
  const Seqno offset = m->seqno() - last_stable;

  if (offset > 0 && offset <= max_out)
    return true;

  if (offset > max_out && m->verify())
  {
    // Send status message to obtain missing messages. This works as a
    // negative ack.
    send_status();
  }

  return false;
}

template<class T>
bool Verifier::in_wv(T *m)
{
  const Seqno offset = m->seqno() - last_stable;

  if (offset > 0 && offset <= max_out && m->view() == view())
    return true;

  if ((m->view() > view() || offset > max_out) && m->verify())
  {
    // Send status message to obtain missing messages. This works as a
    // negative ack.
    send_status();
  }

  return false;
}

// Verify the Protocol Instance Change.
// Return true if it is valid, false otherwise
bool Verifier::verify_protocol_instance_change(Protocol_instance_change* m)
{
#ifdef MSG_DEBUG
  fprintf(stderr, "[verify_protocol_instance_change] Handling message from %d with protocol_instance_id %qd. I am %d and mine is %qd\n", m->sender_id(), m->change_id(), id(), current_protocol_instance);
#endif

  // discard message from myself asap
  if (m->sender_id() == id())
  {
#ifdef MSG_DEBUG
    fprintf(stderr, "[verify_protocol_instance_change] Discard message from myself\n");
#endif
    return false;
  }

  // check the size
  //fixme: for now I hardcode an arbitrary size.
  if (m->size() > 1000)
  {
#ifdef MSG_DEBUG
    fprintf(stderr, "[verify_protocol_instance_change] Message is too big\n");
#endif
    return false;
  }

  // check whether the message is valid or not
  if (!m->verify())
  {
#ifdef MSG_DEBUG
    fprintf(stderr, "[verify_protocol_instance_change] Message not valid\n");
#endif
    return false;
  }

  return true;
}

void Verifier::create_and_send_protocol_instance_change(void) {
#ifdef MSG_DEBUG
  fprintf(stderr, "sending PROTOCOL_INSTANCE_CHANGE\ttime: %qd\n",
        diffTime(currentTime(), first_request_time));
#endif

  Protocol_instance_change *pic = new Protocol_instance_change(
      current_protocol_instance + 1);
  pic->authenticate();

  for (int x = 0; x < num_replicas; x++)
  {
    if (x != id()) {
#if USE_TCP_CONNECTIONS
      sendTCP((Message *) pic, x, snd_socks);
#else
      sendUDP((Message*)pic, x);
#endif
    }
  }

  pic_quorum.bitmap->set(id());
  pic_quorum.nb_msg++;
  pic_quorum.protocol_instance_id
      = MAX(pic_quorum.protocol_instance_id, current_protocol_instance + 1);

  delete pic;
}

void Verifier::handle(Protocol_instance_change* m)
{
#ifdef MSG_DEBUG
  fprintf(stderr, "[handle(Protocol_instance_change)] Handling message from %d with protocol_instance_id %qd. I am %d and mine is %qd\n", m->sender_id(), m->change_id(), id(), current_protocol_instance);
#endif

  // check whether it corresponds at least to the current protocol instance
  if (m->change_id() <= current_protocol_instance)
  {
#ifdef MSG_DEBUG
    fprintf(stderr, "[handle(Protocol_instance_change)] Old protocol_instance_id: %qd <= %qd\n", m->change_id(), current_protocol_instance);
#endif
    return;
  }

  // we already have a message from this node in the quorum
  if (pic_quorum.bitmap->test(m->sender_id()))
  {
#ifdef MSG_DEBUG
    fprintf(stderr, "[handle(Protocol_instance_change)] Bit already set for %d\n", m->sender_id());
#endif
    return;
  }

  // add the message to the quorum
  pic_quorum.bitmap->set(m->sender_id());
  pic_quorum.nb_msg++;
  pic_quorum.protocol_instance_id
      = MAX(pic_quorum.protocol_instance_id, m->change_id());

  // if I have not sent a message, then I check my values and send one if needed
  double master_perf, backup_perf;
  if (!pic_quorum.bitmap->test(id()) && (statsComputer->throughput_not_acceptable(
      &master_perf, &backup_perf) || statsComputer->latency_not_acceptable()))
  {
#ifdef MSG_DEBUG
    fprintf(stderr, "[handle(Protocol_instance_change)] The difference is too high. Sending a Protocol_instance_change\n");
#endif

    create_and_send_protocol_instance_change();
  }

  // if the quorum is complete, then reset it and send a Protocol_instance_change to the PIRs
  if (pic_quorum.nb_msg == 2 * node->f() + 1)
  {
#ifdef MSG_DEBUG
    fprintf(
        stderr,
        "The quorum for protocol instance %qd is complete: %d. Sending a message to the PIRs\n",
        pic_quorum.protocol_instance_id, pic_quorum.nb_msg);
#endif

    fprintf(stderr, "PROTOCOL_INSTANCE_CHANGE\ttime: %qd\n",
        diffTime(currentTime(), first_request_time));
    nb_monitoring_since_last_pic = 0;

    send(m, All_replicas);

    pic_quorum.bitmap->clear();
    pic_quorum.nb_msg = 0;
    current_protocol_instance = pic_quorum.protocol_instance_id;

    replica->statsComputer->reset_latency();

#ifdef MSG_DEBUG
    fprintf(stderr, "[handle(Protocol_instance_change)] My current protocol instance number is %qd\n", current_protocol_instance);
#endif
  }
  else
  {
#ifdef MSG_DEBUG
    fprintf(stderr,
        "The quorum for protocol instance %qd is not yet complete: %d\n",
        pic_quorum.protocol_instance_id, pic_quorum.nb_msg);
#endif
  }
}

void Verifier::handle(Pre_prepare *m)
{
  const Seqno ms = m->seqno();

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

        /*
         printf(
         "\tReplica %i (primary %i) handles a PrePrepare with seqno= %qd from replica %i in view %qd with quorum=%i/%i\n",
         this->id(), primary(), ms, m->id(), m->view(), pc.num_correct(),
         pc.num_complete());
         */

        // Only accept message if we never accepted another pre-prepare
        // for the same view and sequence number and the message is valid.
        //int replica_sender_id = m->id();
        //View replica_sender_view = m->view();
        if (pc.add(m))
        {
#ifdef DELAY_ADAPTIVE
          received_pre_prepares++;
#endif
          //      fprintf(stderr, "\t\t\t\tsending prepare\n");

          /*
           fprintf(stderr, "\tReplica %i (primary %i) sends a Prepare\n", this->id(),
           primary());
           */

          send_prepare(pc);
          if (pc.is_complete())
          {
            send_commit(ms);
          }
          else
          {
            /*
             printf(
             "\tReplica %i (primary %i) handles a PrePrepare with seqno= %qd from replica %i in view %qd, quorum not complete\n",
             this->id(), primary(), ms, replica_sender_id,
             replica_sender_view);
             */
          }
        }
        else
        {
          /*
           printf(
           "\tReplica %i (primary %i) handles a PrePrepare with seqno= %qd from replica %i in view %qd, failed to add the pre-prepare\n",
           this->id(), primary(), ms, replica_sender_id, replica_sender_view);
           */
        }

        return;
      }
      else
      {
        /*
         printf(
         "\tReplica %i (primary %i) handles a PrePrepare with seqno= %qd from replica %i in view %qd, has_new_view() is false\n",
         this->id(), primary(), ms, m->id(), m->view());
         */
      }
    }
    else
    {
      /*
       printf(
       "\tReplica %i (primary %i) handles a PrePrepare with seqno= %qd from replica %i in view %qd, %qd <= %qd\n",
       this->id(), primary(), ms, m->id(), m->view(), ms, low_bound);
       */
    }
  }
  else
  {
    /*
     printf(
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

void Verifier::send_prepare(Prepared_cert& pc)
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

void Verifier::send_commit(Seqno s)
{

  // Executing request before sending commit improves performance
  // for null requests. May not be true in general.
  if (s == last_executed + 1)
    execute_prepared(false);

  Commit* c = new Commit(view(), s);
  //if (print_stuff)
  //  fprintf(stderr, "\t\tSND commit (%qd, %qd)\n", s, c->view());
#ifdef MSG_DEBUG
  fprintf(stderr, "Replica %i (primary=%i): Sending a commit %qd to all replicas\n", id(), primary(), c->seqno());
#endif
  send(c, All_replicas);

  if (s > last_prepared)
    last_prepared = s;

  Certificate<Commit> &cs = clog.fetch(s);
  if (cs.add_mine(c) && cs.is_complete())
  {
    execute_committed();
  }
}

void Verifier::handle(Prepare *m)
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
               printf(
               "\tReplica %i (primary=%i): handles a Prepare with seqno= %qd, certificate not complete\n",
               this->id(), primary(), ms);
               */
            }
          }
          else
          {
            // failed to add
            /*
             printf(
             "\tReplica %i (primary=%i): handles a Prepare with seqno= %qd, failed to add the prepare\n",
             this->id(), primary(), ms);
             */
          }
          return;
        }
        else
        {
          /*
           printf(
           "\tReplica %i (primary=%i): handles a Prepare with seqno= %qd, has_new_view() is false\n",
           this->id(), primary(), ms);
           */
        }
      }
      else
      {
        /*
         printf(
         "\tReplica %i (primary=%i): handles a Prepare with seqno= %qd, %i != %i\n",
         this->id(), primary(), ms, primary(), m->id());
         */
      }
    }
    else
    {
      /*
       printf(
       "\tReplica %i (primary=%i): handles a Prepare with seqno= %qd, %qd <= %qd\n",
       this->id(), primary(), ms, ms, low_bound);
       */
    }
  }
  else
  {
    /*
     printf(
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

void Verifier::handle(Commit *m)
{
  const Seqno ms = m->seqno();

#ifdef MSG_DEBUG
  fprintf(stderr, "Replica %i (primary=%i): handles a Commit with seqno= %qd from %i in view %qd\n", this->id() , primary(), ms, m->id(), m->view());
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
      Certificate<Commit> &cs = clog.fetch(m->seqno());
      /*
       printf(
       "\tReplica %i (primary=%i): handles a Commit with seqno= %qd, quorum size = %i/%i\n",
       this->id(), primary(), ms, cs.num_correct(), cs.num_complete());
       */

      //int replica_sender_id = m->id();
      //View replica_sender_view = m->view();
      if (cs.add(m))
      {
        if (cs.is_complete())
        {
          /*
           printf(
           "\t\tReplica %i (primary=%i): handles a Commit with seqno= %qd from %i in view %qd, executing the command\n",
           this->id(), primary(), ms, replica_sender_id, replica_sender_view);
           */
          execute_committed();
        }
        else
        {
          /*
           printf(
           "\t\tReplica %i (primary=%i): handles a Commit with seqno= %qd from %i in view %qd, quroum not complete\n",
           this->id(), primary(), ms, replica_sender_id, replica_sender_view);
           */
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

void Verifier::handle(Checkpoint *m)
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
        /*
         //fprintf(stderr, "CHEcKPOINT 4\n");
         Certificate<Checkpoint> &cs = elog.fetch(ms);
         if (cs.add(m) && cs.mine() && cs.is_complete())
         {
         */
        //fprintf(stderr, "CHECKPOINT 5\n");
        // I have enough Checkpoint messages for m->seqno() to make it stable.
        // Truncate logs, discard older stable state versions.
        //    fprintf(stderr, "CP MSG call MS %qd!!!\n", last_executed);
        mark_stable(ms, true);
        //}
        return;
      }
    }

    if (m->verify())
    {
      //fprintf(stderr, "CHECKPOINT 6\n");
      // Checkpoint message above my window.

      /*
       if (!m->stable())
       {
       //fprintf(stderr, "CHECKPOINT 7\n");
       // Send status message to obtain missing messages. This works as a
       // negative ack.
       send_status();
       delete m;
       return;
       }
       */

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
          //vtimer->stop();
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

void Verifier::handle(New_key *m)
{
  //fprintf(stderr, "\t\t\t\t\t\tnew key\n");
  if (!m->verify())
  {
    //fprintf(stderr, "BAD NKEY from %d\n", m->id());
  }
  delete m;
}

void Verifier::handle(Status* m)
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
                fprintf(stderr, "Replica %i (primary=%i): Sending a request to %i\n", id(), primary(), m->id());
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
                  fprintf(stderr, "Replica %i (primary=%i): Sending a request to %i\n", id(), primary(), m->id());
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

Pre_prepare* Verifier::prepared(Seqno n)
{
  Prepared_cert& pc = plog.fetch(n);
  if (pc.is_complete())
  {
    return pc.pre_prepare();
  }
  return 0;
}

Pre_prepare *Verifier::committed(Seqno s)
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

bool Verifier::execute_read_only(Request *req)
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
      rep->authenticate(cp, outb.size, true);
      if (outb.size < 50 || req->replier() == node_id || req->replier() < 0)
      {
        // Send full reply.
#ifdef MSG_DEBUG
        fprintf(stderr, "Replica %i (primary=%i): Sending a reply to client %i\n", id(), primary(), cid);
#endif
        send(rep, cid);
      }
      else
      {
        // Send empty reply.
        Reply
            empty(view(), req->request_id(), node_id, rep->digest(), cp, true);
#ifdef MSG_DEBUG
        fprintf(stderr, "Replica %i (primary=%i): Sending an empty reply to client %i\n", id(), primary(), cid);
#endif
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

void Verifier::execute_prepared(bool committed)
{
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
            fprintf(stderr, "Replica %i (primary=%i): Sending an empty reply to client %i\n", id(), primary(), cid);
#endif
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

// execute all the requests in rqueue
void Verifier::execute_committed()
{
  for (Request *req = rqueue.remove(); req != 0; req = rqueue.remove())
  {
    int cid = req->client_id();
#ifdef MSG_DEBUG
    Request_id rid = req->request_id();
#endif

    if (replies.req_id(cid) >= req->request_id())
    {
      // Request has already been executed and we have the reply to
      // the request. Resend reply and don't execute request
      // to ensure idempotence.

#ifdef MSG_DEBUG
      fprintf(stderr,
          "Verifier is sending the reply of request %qu from client %i (already executed)\n",
          rid, cid);
#endif

      replies.send_reply(cid, view(), id());
      continue;
    }

    // Obtain "in" and "out" buffers to call exec_command
    Byz_req inb;
    Byz_rep outb;
    Byz_buffer non_det;
    inb.contents = req->command(inb.size);
    outb.contents = replies.new_reply(cid, outb.size);
    non_det.contents = NULL;

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
#ifdef MSG_DEBUG
      fprintf(stderr,
          "Verifier is executing request %qu from client %i\n",
          rid, cid);
#endif

      exec_command(&inb, &outb, &non_det, cid, false, exec_command_delay);
      if (outb.size % ALIGNMENT_BYTES)
        for (int i = 0; i < ALIGNMENT_BYTES - (outb.size % ALIGNMENT_BYTES); i++)
          outb.contents[outb.size + i] = 0;
      //if (last_tentative_execute%100 == 0)
      //  fprintf(stderr, "%s - %qd\n",((node_id == primary()) ? "P" : "B"), last_tentative_execute);
    }

    // Finish constructing the reply.
    replies.end_reply(cid, req->request_id(), outb.size);

    // Send the reply. Replies to recovery requests are only sent
    // when the request is committed.
    if (outb.size != 0 && !is_replica(cid))
    {
      if (outb.size < 50 || req->replier() == node_id || req->replier() < 0)
      {
        // Send full reply.
#ifdef MSG_DEBUG
        fprintf(stderr,
            "Verifier is sending reply for request %qu to client %i\n",
            rid, cid);
#endif

        replies.send_reply(cid, view(), id(), false);
      }
      else
      {
        // Send empty reply
        Reply empty(view(), req->request_id(), node_id, replies.digest(cid),
            i_to_p(cid), false);
#ifdef MSG_DEBUG
        fprintf(stderr, "Replica %i (primary=%i): Sending an empty reply to client %i\n", id(), primary(), cid);
#endif

        send(&empty, cid);
      }

      replies.commit_reply(cid);
    }
  }
}

void Verifier::update_max_rec()
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

void Verifier::new_state(Seqno c)
{
  //  fprintf(stderr, ":n)e:w):s)t:a)t:e) ");
  if (vi.has_new_view(v) && c >= low_bound)
    has_nv_state = true;

  if (c > last_executed)
  {
    last_executed = last_tentative_execute = c;
    //    fprintf(stderr, ":):):):):):):):) (new_state) Set le = %d\n", last_executed);
    if (replies.new_state(&rqueue))
      ; //vtimer->stop();

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
  execute_committed();

  // Execute any buffered read-only requests
  for (Request *m = ro_rqueue.remove(); m != 0; m = ro_rqueue.remove())
  {
    execute_read_only(m);
    delete m;
  }

  if (rqueue.size() > 0)
  {
    if (primary() == id())
    {
      // Send pre-prepares for any buffered requests
      send_pre_prepare();
    }
    else
      //fprintf(stderr, "vtimer restart (2)\n");
      ;//vtimer->restart();
  }
}

void Verifier::mark_stable(Seqno n, bool have_state)
{
  //XXXXXcheck if this should be < or <=

  //  fprintf(stderr, "mark stable n %qd laststable %qd\n", n, last_stable);
  if (n <= last_stable)
    return;

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
  }
  //  else
  //    fprintf(stderr, "OH BASE! OH CLU!\n");

  if (last_stable > seqno)
    seqno = last_stable;

  //  fprintf(stderr, "mark_stable: Truncating plog to %ld have_state=%d\n", last_stable+1, have_state);
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
        Certificate<Checkpoint> &cs = elog.fetch(cn);
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
  if (primary() == id())
    send_pre_prepare();
}

void Verifier::handle(Data *m)
{
  //fprintf(stderr, "\t\t\t\t\t\tdata\n");
  //fprintf(stderr, "received data\n");
  state.handle(m);
}

void Verifier::handle(Meta_data *m)
{
  //fprintf(stderr, "\t\t\t\t\t\tmetadata\n");

  state.handle(m);
}

void Verifier::handle(Meta_data_d *m)
{
  //fprintf(stderr, "\t\t\t\t\t\tmetadatad\n");

  state.handle(m);
}

void Verifier::handle(Fetch *m)
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

void Verifier::send_new_key()
{
  Node::send_new_key();
  fprintf(stderr, " call to send new key\n");
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

void Verifier::send_status(bool force)
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

void Verifier::handle(Reply *m, bool mine)
{
  //  th_assert(false,"should not be there...\n");
  int mid = m->id();
  int mv = m->view();

#ifdef MSG_DEBUG
  fprintf(stderr, "Replica %i (primary=%i): handles a Reply of id %i and view %i\n", this->id(), primary(), mid, mv);
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

void Verifier::send_null()
{
  //fprintf(stderr, "@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@\n");
  th_assert(id() == primary(), "Invalid state");

  Seqno max_rec_point = max_out + (max_rec_n + checkpoint_interval - 1)
      / checkpoint_interval * checkpoint_interval;

  if (max_rec_n && max_rec_point > last_stable && has_new_view())
  {
    if (rqueue.size() == 0 && seqno <= last_executed && seqno + 1 <= max_out
        + last_stable)
    {
      // Send null request if there is a recovery in progress and there
      // are no outstanding requests.
      seqno++;
      Req_queue empty;
      Pre_prepare* pp = new Pre_prepare(view(), seqno, empty);
#ifdef MSG_DEBUG
      fprintf(stderr, "Replica %i (primary=%i): sending pre-prepare to all replicas\n", this->id(), primary());
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
  //fprintf(stderr, " ###### @@ vtimer expired! @@ ######\n");
  th_assert(replica, "replica is not initialized\n");
  if (!replica->delay_vc())
  {
    fprintf(
        stderr,
        "Replica %i, primary %i, view %qd : sending view change (vtimer expired)\n",
        replica->id(), replica->primary(), replica->view());
    replica->send_view_change();
  }
  else
  {
    //fprintf(stderr, "restarting vtimer 4\n");
    //fprintf(stderr, "vtimer restart (3)\n");
    //replica->vtimer->restart();
  }
}

void delay_pre_prepare_timer_handler()
{
  if (replica->has_new_view() && replica->primary() == replica->id())
    replica->send_pre_prepare(true);
}

void stimer_handler()
{
  //fprintf(stderr, "--- stimer expired --- \n");
  th_assert(replica, "replica is not initialized\n");
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

void statTimer_handler()
{
  // save the time at which the timer has been called for the first time
  if (replica->statTimer_first_call == 0 && replica->first_request_time != 0)
  {
    replica->statTimer_first_call = diffTime(currentTime(),
        replica->first_request_time);
  }

  replica->statsComputer->compute_performance_of_pirs();

#ifndef PROTOCOL_INSTANCE_CHANGE_DEACTIVATED
  double thr_master_perf, thr_backup_perf;
  thr_master_perf = thr_backup_perf = 0;
  if (++(replica->nb_monitoring_since_last_pic) > MONITORING_GRACE_PERIOD && replica->statsComputer->throughput_not_acceptable(&thr_master_perf, &thr_backup_perf))
  {
#ifdef MSG_DEBUG
    fprintf(stderr, "The difference is too high: thr_master=%f, thr_backup=%f. Sending a Protocol_instance_change\n", thr_master_perf, thr_backup_perf);
#endif

    replica->create_and_send_protocol_instance_change();
  }
#endif // PROTOCOL_INSTANCE_CHANGE_DEACTIVATED

  //Emptying the statsComputer data structures
  replica->statsComputer->reset_throughput();

  //Restarting Timer
  replica->statTimer->restart();
}

#ifdef DEBUG_PERIODICALLY_PRINT_THR
static int pipes_thr_handler_id = 0;

void pipes_thr_handler()
{
  Time now = currentTime();
  float elapsed_in_sec = diffTime(now, replica->pipes_thr_start) / 1000000.0;
  float time_in_ms = diffTime(now, 0) / 1000.0;

  unsigned long n=0;
  for (int i=0; i<replica->num_clients(); i++) {
     n += replica->certificates[i].size();
  }
  fprintf(stderr, "T=%f, there are %lu certificates\n", time_in_ms, n);


  fprintf(stderr, "===== Pipes throughput handler %d =====\n", pipes_thr_handler_id++);
  fprintf(stderr, "*-* nb_received_requests=%f msg/sec\n", replica->nb_received_requests / elapsed_in_sec);
  fprintf(stderr, "*-* nb_sent_requests_to_forwarder=%f msg/sec\n", replica->nb_sent_requests_to_forwarder / elapsed_in_sec);
  fprintf(stderr, "*-* nb_recv_requests_from_verifier_thr=%f msg/sec\n", replica->nb_recv_requests_from_verifier_thr / elapsed_in_sec);
  fprintf(stderr, "*-* nb_sent_propagate=%f msg/sec\n", replica->nb_sent_propagate / elapsed_in_sec);
  for(int i=0; i<replica->n(); i++)
    fprintf(stderr, "*-* nb_recv_propagate[%d]=%f msg/sec\n", i, replica->nb_recv_propagate[i] / elapsed_in_sec);
  fprintf(stderr, "*-* nb_sent_requests_to_verifier=%f msg/sec\n", replica->nb_sent_requests_to_verifier / elapsed_in_sec);
  fprintf(stderr, "*-* nb_recv_requests_from_forwarder=%f msg/sec\n", replica->nb_recv_requests_from_forwarder / elapsed_in_sec);
  fprintf(stderr, "*-* nb_sent_requests_to_replicas=%f msg/sec\n", replica->nb_sent_requests_to_replicas / elapsed_in_sec);
  for(int i=0; i<replica->num_pirs(); i++)
    fprintf(stderr, "*-* nb_recv_requests_from_replicas[%d]=%f msg/sec\n", i, replica->nb_recv_requests_from_replicas[i] / elapsed_in_sec);
  fprintf(stderr, "*-* nb_sent_requests_to_exec=%f msg/sec\n", replica->nb_sent_requests_to_exec / elapsed_in_sec);
  fprintf(stderr, "*-* nb_recv_requests_from_verifier=%f msg/sec\n", replica->nb_recv_requests_from_verifier / elapsed_in_sec);
  fprintf(stderr, "*-* nb_sent_replies=%f msg/sec\n", replica->nb_sent_replies / elapsed_in_sec);
  fprintf(stderr, "==========\n");

  __sync_and_and_fetch(&replica->nb_received_requests, 0);
  __sync_and_and_fetch(&replica->nb_sent_requests_to_forwarder, 0);
  __sync_and_and_fetch(&replica->nb_recv_requests_from_verifier_thr, 0);
  __sync_and_and_fetch(&replica->nb_sent_propagate, 0);
  for(int i=0; i<replica->n(); i++) __sync_and_and_fetch(&replica->nb_recv_propagate[i], 0);
  __sync_and_and_fetch(&replica->nb_sent_requests_to_verifier, 0);
  __sync_and_and_fetch(&replica->nb_recv_requests_from_forwarder, 0);
  __sync_and_and_fetch(&replica->nb_sent_requests_to_replicas, 0);
  for(int i=0; i<replica->num_pirs(); i++) __sync_and_and_fetch(&replica->nb_recv_requests_from_replicas[i], 0);
  __sync_and_and_fetch(&replica->nb_sent_requests_to_exec, 0);
  __sync_and_and_fetch(&replica->nb_recv_requests_from_verifier, 0);
  __sync_and_and_fetch(&replica->nb_sent_replies, 0);
  replica->pipes_thr_start = currentTime();

  replica->pipes_thr_timer->restart();
}
#endif

#ifdef PERIODICALLY_MEASURE_THROUGHPUT
void periodic_thr_measure_handler(void)
{
  if (replica->next_measure_idx < MAX_NB_MEASURES)
  {
    float elapsed_usec = diffTime(currentTime(),
        replica->start_cycle_4_periodic_thr_measure);
    float throughput = replica->nb_requests_4_periodic_thr_measure
        / elapsed_usec * 1000000.0;
    replica->measured_throughput[replica->next_measure_idx] = throughput;

    /*
     fprintf(
     stderr,
     "Calling periodic_thr_measure_handler after %f sec. %d reqs have been executed, for a throughput of %f req/s\n",
     elapsed_usec / 1000000.0, replica->nb_requests_4_periodic_thr_measure,
     throughput);
     */

    replica->next_measure_idx++;
    replica->nb_requests_4_periodic_thr_measure = 0;
    replica->start_cycle_4_periodic_thr_measure = currentTime();

    replica->periodic_thr_measure->restart();
  }
}
#endif

void Verifier::read_all_remaining_messages(void)
{
  fd_set file_descriptors; //set of file descriptors to listen to (only one socket, in this case)
  timeval listen_time; //max time to wait for something readable in the file descriptors

  fprintf(stderr, "========== read all remaining messages ==========\n");

  while (1) {
    FD_ZERO(&file_descriptors); //initialize file descriptor set

    FD_SET(pirs_to_verifier_fd[0], &file_descriptors);
    int maxsock = pirs_to_verifier_fd[0];

    for (int j = 1; j < num_pirs(); j++)
    {
      FD_SET(pirs_to_verifier_fd[j], &file_descriptors);
      maxsock = MAX(maxsock, pirs_to_verifier_fd[j]);
    }

    FD_SET(request_buffer->fd,&file_descriptors);
    maxsock = MAX(maxsock, request_buffer->fd);

    listen_time.tv_sec = 0;
    listen_time.tv_usec = 500;

    int s = select(maxsock + 1, &file_descriptors, NULL, NULL, &listen_time);
    if (s <= 0) {
      break;
    }

    for (int x = 0; x < num_pirs(); x++)
    {
      if (FD_ISSET(pirs_to_verifier_fd[x], &file_descriptors))
      {
        Message *mp = new Message(Max_message_size);
        int ret = read(pirs_to_verifier_fd[x], mp->contents(), mp->msize());

        if (ret >= (int) sizeof(Message_rep) && ret >= mp->size() && mp->tag() == Wrapped_request_tag && x == 0)
        {
          Wrapped_request *wr = (Wrapped_request*) mp;
          fprintf(stderr, "Found Wrapped_request %qd from %d in kzimp\n", wr->seqno(), x);
        }
        delete mp;
      } // end FD_ISSET
    } // end for each pir
    

    if (FD_ISSET(request_buffer->fd, &file_descriptors)) {
      Message *m = request_buffer->cb_read_msg();
      if (m && m != request_buffer->cb_magic())
      {
        if (m->tag() == Request_tag)
        {
          Request *r = (Request*)m;
          fprintf(stderr, "Request (%d, %qd) from the request buffer\n", r->client_id(), r->request_id());
        }
        else
        {
          fprintf(stderr, "Message with tag %i from the request buffer\n", m->tag());
        }
        delete m;
      }
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

#ifdef MSG_DEBUG
    // PRINT VERIFIED REQS AND HOLES
    fprintf(stderr, "===== Verifier thread =====\n");
    for (int i = 0; i < replica->num_clients(); i++)
    {
      if (replica->min_verified_rid[i] > 0 || replica->max_verified_rid[i] > 0)
      {
        fprintf(stderr, "-- Client %d\nmin_verified=%qd, max_verified=%qd, %d missing reqs\n",
            i + replica->num_replicas, replica->min_verified_rid[i],
            replica->max_verified_rid[i], replica->missing_reqs[i].size());
        for (std::map<Request_id, char>::iterator it =
            replica->missing_reqs[i].begin(); it != replica->missing_reqs[i].end(); it++)
        {
          fprintf(stderr, "\t%qd is missing\n", it->first);
        }
      }
    }
    fprintf(stderr, "\n");

    // PRINT LIST OF CERTIFICATES
    fprintf(stderr, "===== Forwarder thread =====\n");
    for (int i = 0; i < replica->num_clients(); i++)
    {
      if (replica->certificates[i].size() > 0) {
        fprintf(stderr, "-- Client %d, %d certificates\n",
            i + replica->num_replicas, replica->certificates[i].size());
        for (std::unordered_map<Request_id, forwarding_cert>::iterator it =
            replica->certificates[i].begin(); it
            != replica->certificates[i].end(); it++)
        {
          fprintf(stderr, "\tRequest (%d, %qd): quorum is %d\n",
              i + replica->num_replicas, it->first,
              it->second.fwd_bitmap->total_set());
        }
      }
    }
    fprintf(stderr, "\n");


    // PRINT REPLIES
    fprintf(stderr, "===== Executor thread =====\n");
    for (int i = 0; i < replica->num_clients(); i++)
    {
      if (replica->min_exec_rid[i] > 0) {
        fprintf(stderr, "Client %d, min_exec_rid=%qd, %d replies\n", i+replica->num_replicas, replica->min_exec_rid[i], replica->last_replies[i].size());
        for (std::map<Request_id, Reply*>::iterator it =
              replica->last_replies[i].begin(); it
              != replica->last_replies[i].end(); it++) {
          fprintf(stderr, "\tReply for req %qd is present\n", it->first);
        }
      }
    }
    fprintf(stderr, "\n");
#endif


    // PRINT MONITORING STATS
    fprintf(stderr,
        "statTimer_time_first_call= %qd usec\tmonitoring_period= %d usec\n",
        replica->statTimer_first_call, MONITORING_PERIOD * 1000);
    fprintf(stderr, "number of executed requests: %ld\n", replica->nb_executed);

    replica->statsComputer->print_stats();

#ifdef PERIODICALLY_MEASURE_THROUGHPUT
    fprintf(stderr, "Periodic throughputs:\n");
    for (int i = 0; i < replica->next_measure_idx; i++)
    {
      fprintf(stderr, "%f\n", replica->measured_throughput[i]);
    }
#endif

    replica->statsComputer->print_latencies_for_offline_analysis();
    delete replica->statsComputer;

    sleep(3600);
  }
}

bool Verifier::has_req(int cid, const Digest &d)
{
  Request* req = rqueue.first_client(cid);

  if (req && req->digest() == d)
    return true;

  return false;
}

Message* Verifier::pick_next_status()
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

void Verifier::handle(View_change *m)
{
#ifdef MSG_DEBUG
  fprintf(stderr, "Replica %i  (primary=%i) (view %qd) handles a View change from %i for view %qd\n", this->id(), primary(), view(), m->id(), m->view());
#endif

  //muting replicas

  if (has_new_view() && m->view() > view())
  {
    // it seems that we get here only at the beginning of the execution.
    //fprintf(stderr, "Replica %i  (primary=%i) (view %qd): muted replica %d\n",
    //    this->id(), primary(), m->id());

    excluded_replicas[m->id()] = true;
  }

  //int size_of_vc = sizeof(View_change_rep) + sizeof(Req_info) * m->rep().n_reqs;

  /*
   printf(
   "Replica %i, primary %i, view %qd, checking received view change of size %i with seqno= %qd\n",
   id(), primary(), view(), size_of_vc, m->last_stable());
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
      //vtimer->restart();
      limbo = false;
      vc_recovering = true;
    }
  }
}

void Verifier::handle(New_view *m)
{
  //fprintf(stderr, "\t\t\t\t\t\tnew view\n");
  //  fprintf(stderr, "RECV: new view v=%qd from %d\n", m->view(), m->id());

#ifdef MSG_DEBUG
  fprintf(stderr, "Replica %i (primary=%i) (view %qd) handles a New view from %d for view %qd\n", this->id(), primary(), view(), m->id(), m->view());
#endif

  vi.add(m);

#ifdef FAIRNESS_ADAPTIVE

  for (int j = 0; j < num_principals; j++)
  {
    //I may have stale informations in my execute_snapshot array... resetting it.
    executed_snapshot[j] = -1;
  }

#endif
}

void Verifier::handle(View_change_ack *m)
{
  //fprintf(stderr, "RECV: view-change ack v=%qd from %d for %d\n", m->view(), m->id(), m->vc_id());

#ifdef MSG_DEBUG
  fprintf(stderr, "Replica %i (primary=%i) (view %qd) handles a View change ack from %i for %i for view %qd\n", this->id(), primary(), view(), m->id(), m->vc_id(), m->view());
#endif

  vi.add(m);
}

void Verifier::send_view_change()
{
  // Do not send the view change if view changes are deactivated.
#ifdef VIEW_CHANGE_DEACTIVATED
  return;
#endif

  // Move to next view.

  //unmuting replicas
  for (int j = 0; j < num_replicas; j++)
    excluded_replicas[j] = false;
  fprintf(stderr, "all replicas unmuted\n");

#ifdef DELAY_ADAPTIVE
  pre_prepare_timer->restop();
#endif
  v++;
  fprintf(stderr, "sending a view_change, v: %qd, last_executed: %qd \n", v,
      last_executed);
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
  //vtimer->stop(); // stop timer if it is still running
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
    Certificate<Commit> &cc = clog.fetch(i);

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

void Verifier::process_new_view(Seqno min, Digest d, Seqno max, Seqno ms)
{
  th_assert(ms >= 0 && ms <= min, "Invalid state");

  fprintf(stderr,
      "XXX process new view: %qd, min: %qd, max: %qd, ms: %qd, time: %qd\n", v,
      min, max, ms, rdtsc());

  not_deprioritize_status_before_this = max + 1; //just to be sure... ;)

  //vtimer->restop();
  limbo = false;
  vc_recovering = true;

  if (primary(v) == id())
  {
    New_view* nv = vi.my_new_view();
#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i (primary %i) in view %qd is sending a new view to all replicas\n", id(), primary(), view());
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
      fprintf(stderr, "Replica %i, primary %i, view %qd: Sending a prepare to all replicas from process new view\n", id(), primary(), view());
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
    //vtimer->restart();
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
#ifdef THROUGHPUT_ADAPTIVE_LAST_VIEW
  req_count_vc = 0;
  last_view_time = rdtsc();
#endif
#endif

  //unmuting clients
  excluded_clients = false;

  print_stuff = true;
  //fprintf(stderr, "DONE:process new view: %qd, has new view: %d\n", v, has_new_view());
}
