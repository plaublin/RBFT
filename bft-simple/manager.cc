/*
 * manager.cc
 * Send requests to clients, get the result & compute stats
 */

#include <stdio.h>
#include <strings.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <netinet/tcp.h> // for TCP_NODELAY
#include <errno.h>
#include <math.h>

#include "th_assert.h"
#include "Timer.h"
#include "stats_utils.h"
#include "tcp_net.h"
#include "simple_benchmark.h"
#include "Client.h"

#include <vector>
using namespace std;

// array of sockets for comm with clients
int *accepted_client_sockets_fd;

// the socket on which clients connect to
int listen_clients_socket_fd;

// where to output the results
FILE* F;

// global variables
int num_faults = 0;
int num_replicas = 1;
int num_clients = 1;
int num_req = 1000;
int timeout = 120;
int nb_bursts_to_ignore = 5;
int nb_bursts_total = 10;

// the signal
int next_xp = 0;
int nb_sig = 0;
void next_xp_fn(int sig)
{
  if (sig == SIGINT)
  {
    next_xp = 1;
    nb_sig++;
  }

  if (nb_sig == 3)
  {
    fprintf(stderr, "Exiting after 3 SIGINT\n");
    exit(0);
  }
}

/*
 * initialize everything (basically sockets)
 */
int init(void)
{
  if (num_clients > 0)
  {
    //client_addrs = new Address[num_clients];
    accepted_client_sockets_fd = (int*) malloc(sizeof(int) * num_clients);
  }
  else
  {
    fprintf(stderr, "nb clients can't be <0 (its value was %i)!\n", num_clients);
    return -1;
  }

  // 1) Create socket to be bound next.
  listen_clients_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_clients_socket_fd == -1)
  {
    perror("Error while creating the socket! ");
    exit(errno);
  }

  // TCP NO DELAY
  int flag = 1;
  int result = setsockopt(listen_clients_socket_fd,
      IPPROTO_TCP,
      TCP_NODELAY,
      (char*) &flag,
      sizeof(int));

  if (result == -1) {
    fprintf(stderr, "Unable to set TCP_NODELAY for socket listening for clients connections, exiting.\n");
    exit(-1);
  }

  struct sockaddr_in a;
  bzero((char *) &a, sizeof(a));
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_family = AF_INET;
  a.sin_port = htons(MANAGER_PORT);

  // 2) Bind socket to port.
  if (bind(listen_clients_socket_fd, (struct sockaddr*) &a, sizeof(a)) == -1)
  {
    perror("Error while binding to the socket! ");
    exit(errno);
  } else

  fprintf(stderr, "Manager successfully bound listening socket on port: %i\n", MANAGER_PORT);


  // 3) make the socket listening for incoming connections
  if (listen(listen_clients_socket_fd, num_clients + 1) == -1)
  {
    perror("Error while calling listen! ");
    exit(errno);
  }

  // 4) Actually accept each client connection and store the accepted socket fd in the array.
  for (int i = 0; i < num_clients; i++)
  {
    struct sockaddr_in csin;
    int sinsize = sizeof(csin);
    accepted_client_sockets_fd[i] = accept(listen_clients_socket_fd,
        (struct sockaddr*) &csin,
        (socklen_t*) &sinsize);

    if (accepted_client_sockets_fd[i] == -1)
    {
      perror("An invalid socket has been accepted! ");
      continue;
    }

    // TCP NO DELAY
    flag = 1;
    int result = setsockopt(accepted_client_sockets_fd[i],
        IPPROTO_TCP,
        TCP_NODELAY,
        (char*) &flag,
        sizeof(int));

    if (result == -1) {
      fprintf(stderr, "Unable to set TCP_NODELAY on accepted connection from client, exiting.\n");
      exit(-1);
    }

    // add the socket to the array of sockets
//#ifdef DEBUG
    fprintf(stderr, "A connection has been accepted from client %s:%i (socket %d)\n",
        inet_ntoa(csin.sin_addr),
        ntohs(csin.sin_port), accepted_client_sockets_fd[i]);
//#endif
  }

  return 0;
}

/*
 * function to send ONE request to ALL clients
 */
int send_request(ManReq req)
{
#ifdef DEBUG
  //fprintf(stderr, "Sending request:\n\ttype = %i\n\tnb requests = %i\n\trequests size = %i\n",
  //    req.type, req.nb_requests, req.size);
#endif

  // sending req to each client
  for (int c = 0; c < num_clients; c++)
  {
    sendMsg(accepted_client_sockets_fd[c], (void*) &req, sizeof(req));
  }

  return 0;
}

ManReq receive_message_from_client()
{
  int ret;
  int fd_from_which_we_received_a_message;
  fd_set set_read, set_write, set_exc;
  struct timeval tv;
  ManReq in;

  while (true)
  {
    tv.tv_sec = timeout; // timeout in seconds
    tv.tv_usec = 0;

    // init sets
    FD_ZERO(&set_read);
    FD_ZERO(&set_write);
    FD_ZERO(&set_exc);
    int max = 0;
    for (int i = 0; i < num_clients; i++)
    {
      FD_SET(accepted_client_sockets_fd[i], &set_read);
      max = ((accepted_client_sockets_fd[i] > max) ? accepted_client_sockets_fd[i] : max);
    }

    ret = select(max + 1, &set_read, &set_write, &set_exc, &tv);
    if (ret)
    {
      for (int i = 0; i < num_clients; i++)
      {
        if (FD_ISSET(accepted_client_sockets_fd[i], &set_read))
        {
          ret = recvMsg(accepted_client_sockets_fd[i], &in, sizeof(in));
          fd_from_which_we_received_a_message = i;
          break;
        }
      }
    }

    if (!ret || ret != sizeof(in) || in.type != mr_response)
    {
      //fprintf(stderr, "Error: ret=%i, sizeof(in)=%i, type=%i\n", ret, sizeof(in), in.type);
      fd_from_which_we_received_a_message = -1;
    }
    else
    {
      break;
    }
  }

  in.client_id = fd_from_which_we_received_a_message;

  return in;
}

/* launch experiments */
void start_experiment(int size, float limit_thr, int nb_max_req)
{
  ManReq req;
  Timer t;
  float avg_thr, avg_lat, thr_stddev, lat_stddev, current_thr, current_lat;
  unsigned long total_nb_req;

  int nb_bursts_so_far;
  vector<float>* list_of_thr = new vector<float>();
  vector<float>* list_of_lat = new vector<float>();

  int req_size, rep_size;

  req_size = size;
  rep_size = size;

  req.type = mr_burst;
  req.nb_requests = num_req;
  req.size = req_size;
  req.limit_thr = ceil((double) limit_thr / (double) num_clients);
  req.start_logging = 1;

  total_nb_req = 0;
  avg_thr = 0;
  avg_lat = 0;
  thr_stddev = 0;
  lat_stddev = 0;
  nb_bursts_so_far = 0;

  if ((F = fopen("manager.log", "w")) != NULL)
  {
    fprintf(F, "#num_clients=%i\n#req_size=%i\n#rep_size=%i\n#limit_thr=%f\n",
        num_clients, req_size, rep_size, limit_thr);
    fprintf(
          F,
          "#nb faults: %i\n#nb clients: %i\n#nb_bursts_to_ignore: %i\n#nb_bursts_total: %i\n#req_size: %i\n#nb_max_req: %i\n",
          num_faults, num_clients, nb_bursts_to_ignore, nb_bursts_total, req_size,
          nb_max_req);
    fprintf(
        F,
        "#total_nb_req\tthr\tthr_stddev\tlat\tlat_stddev\tcurrent_throughput\tcurrent_latency\n");
    fflush(F);
  }

  fprintf(stderr,
      "#num_clients=%i\n#req_size=%i\n#rep_size=%i\n#limit_thr=%f\n",
      num_clients, req_size, rep_size, limit_thr);
  fprintf(
      stderr,
      "#total_nb_req\tthr\tthr_stddev\tlat\tlat_stddev\tcurrent_throughput\tcurrent_latency\n");

  // send the start burst, for the clients to start
  send_request(req);

  ManReq* results_from_clients = (ManReq*) malloc(sizeof(ManReq) * num_clients);

  for (int i = 0; i < num_clients; i++)
  {
    results_from_clients[i].client_id = -1;
  }

  // XXX(apace): this is a ugly hard-coded hack.
  nb_bursts_to_ignore = 5;
  nb_bursts_total = 10;
  
#ifdef MANAGER_FOR_FLUCTUATING_LOAD_TRACE
  // start time
  long long start_time = currentTime();
#endif

  while (true)
  {
    // stop the experiment
    if (nb_bursts_so_far >= nb_bursts_total /*&& total_nb_req > nb_max_req*/)
      break;

    int nb_messages_received = 0;
    while (nb_messages_received < num_clients)
    {
      ManReq tmp = receive_message_from_client();

      if (tmp.client_id < 0 || tmp.client_id >= num_clients)
      {
        /*
         fprintf(stderr, "New message from %i, the id is not valid!!!!!",
         tmp.client_id);
         */
        continue;
      }
      else
      {
        // the message is valid
        int previous_cid = results_from_clients[tmp.client_id].client_id;
        results_from_clients[tmp.client_id] = tmp;

        // is it a message from a client for which we do not already have one?
        if (previous_cid == -1)
        {
          nb_messages_received++;
        }

#ifdef DEBUG
         fprintf(stderr,
         "I have received a new message from client %i, %i left\n",
         tmp.client_id, num_clients - nb_messages_received);
#endif
      }
    }
    
#ifdef MANAGER_FOR_FLUCTUATING_LOAD_TRACE
    // stop time
    long long end_time = currentTime();
#endif

    // aggregate values
    /* For each ManReq in the array, we have:
     -who is the client: out.client_id
     -latency mean of that client: out.latency_mean
     -nb correct requests executed by that client: out.nb_requests = nb_correct_sent
     -current throughput of the client: out.limit_thr = current_thr;
     */

    current_lat = 0;
    current_thr = 0;
    int num_correct_clients = 0;
    //we consider now only results coming from correct clients
    //correct clients should have a throughput > 0

    for (int i = 0; i < num_clients; i++)
    {
        //fprintf(stderr,"Client %d, lat= %f, thr= %f\n",i,results_from_clients[i].latency_mean,results_from_clients[i].limit_thr);
        if(results_from_clients[i].latency_mean > 0){
            num_correct_clients++;
            total_nb_req += results_from_clients[i].nb_requests;
            current_lat += results_from_clients[i].latency_mean;
            current_thr += results_from_clients[i].limit_thr;
        }
    }
    //fprintf(stderr,"The number of correct clients is %d\n",num_correct_clients);
    //Old code where all clients were correct
    //current_lat /= (float) num_clients;
    
    // if there is no correct clients then I exit, since I do not really want
    // to manage only faulty clients (I am a little rationnal :))
    if (num_correct_clients == 0) {
        fprintf(stderr, "There is no correct clients. I do not want to continue (I'm a little rationnal)\n");
        goto out;
    }

    //we consider now only results coming from correct clients
    current_lat /= (float) num_correct_clients;

#ifdef MANAGER_FOR_FLUCTUATING_LOAD_TRACE
    // compute throughput
    current_thr = total_nb_req / float(diffTime(end_time, start_time)) * 1000000.0;
#endif

    // add the latency and the throughput if it is time
    if (nb_bursts_so_far >= nb_bursts_to_ignore)
    {
      list_of_lat->push_back(current_lat);
      list_of_thr->push_back(current_thr);
    }

    // compute average and stddev in the lists
    avg_thr = vector_compute_avg(list_of_thr);
    thr_stddev = vector_compute_stddev(list_of_thr, avg_thr);
    avg_lat = vector_compute_avg(list_of_lat);
    lat_stddev = vector_compute_stddev(list_of_lat, avg_lat);

    if (nb_bursts_so_far >= nb_bursts_to_ignore)
    {
      fprintf(stderr, "%lu\t%f\t%f\t%f\t%f\t%f\t%f\n", total_nb_req, avg_thr,
          thr_stddev, avg_lat, lat_stddev, current_thr, current_lat);
      if (F != NULL)
      {
        fprintf(F, "%lu\t%f\t%f\t%f\t%f\t%f\t%f\n", total_nb_req, avg_thr,
            thr_stddev, avg_lat, lat_stddev, current_thr, current_lat);
        fflush(F);
      }
    }
    else
    {
      fprintf(stderr, "%lu\t%f\t%f\t%f\t%f\t%f\t%f\t(*)\n", total_nb_req,
          avg_thr, thr_stddev, avg_lat, lat_stddev, current_thr, current_lat);
      if (F != NULL)
      {
        fprintf(F, "%lu\t%f\t%f\t%f\t%f\t%f\t%f\t(*)\n", total_nb_req, avg_thr,
            thr_stddev, avg_lat, lat_stddev, current_thr, current_lat);
        fflush(F);
      }
    }

    for (int i = 0; i < num_clients; i++)
    {
      results_from_clients[i].client_id = -1;
    }

    nb_bursts_so_far++;

    fprintf(stderr, "#nb_bursts_so_far=%d out of %d total to do\n", nb_bursts_so_far, nb_bursts_total);

#ifdef MANAGER_FOR_FLUCTUATING_LOAD_TRACE
    // fluctuating load trace, recv only 1 burst
    break;
#endif
  }

out:
  delete list_of_lat;
  delete list_of_thr;
  next_xp = 0;
  fclose(F);
}

/*
 * main ;)
 */
int MANAGER_MAIN(int argc, char **argv)
{
  int nb_req_before_logging = 200000;
  int nb_max_req = nb_req_before_logging * 2;
  int req_size = 0;

  // add signal
  signal(SIGINT, next_xp_fn);

  // process command line options
  int opt;
  while ((opt = getopt(argc, argv, "f:c:n:t:b:l:s:")) != EOF)
  {
    switch (opt)
    {
    case 'f':
      num_faults = atoi(optarg);
      num_replicas = 3 * num_faults + 1;
      break;

    case 'c':
      num_clients = atoi(optarg);
      break;

    case 'n':
      num_req = atoi(optarg);
      break;

    case 't':
      timeout = atoi(optarg);
      break;

    case 'b':
      nb_req_before_logging = atoi(optarg);
      break;

    case 'l':
      nb_max_req = atoi(optarg);
      break;

    case 's':
      req_size = atoi(optarg);
      break;

    default:
      fprintf(
          stderr,
          "%s -f nb_faults -c nb_clients -n nb_req -t timeout -b nb_req_before_logging -l nb_max_req -s req_size\n",
          argv[0]);
      exit(-1);
    }
  }

  nb_bursts_to_ignore = nb_req_before_logging / (num_clients * num_req);
  nb_bursts_total = nb_max_req / (num_clients * num_req);

  fprintf(
      stderr,
      "Launching manager...\n\tnb faults: %i\n\tnb clients: %i\nnb_bursts_to_ignore: %i\nnb_bursts_total: %i\nreq_size: %i\nnb_max_req: %i\n",
      num_faults, num_clients, nb_bursts_to_ignore, nb_bursts_total, req_size,
      nb_max_req);

  // initializations
  init_clock_mhz();
  init();

  // send requests
  // without limit_thr (in fact with a very high limit)
  if (req_size == 8) {
    start_experiment(req_size, 35000, nb_max_req);
    //start_experiment(req_size, 21000, nb_max_req);
  } else if (req_size == 100) {
    //start_experiment(req_size, 31000, nb_max_req);
    start_experiment(req_size, 19000, nb_max_req);
  } else if (req_size == 500) {
    //start_experiment(req_size, 21000, nb_max_req);
    start_experiment(req_size, 13000, nb_max_req);
  } else if (req_size == 1000) {
    //start_experiment(req_size, 15000, nb_max_req);
    //start_experiment(req_size, 9000, nb_max_req);
    start_experiment(req_size, 8800, nb_max_req);
  } else if (req_size == 2000) {
    //start_experiment(req_size, 10000, nb_max_req);
    start_experiment(req_size, 6000, nb_max_req);
  } else if (req_size == 3000) {
    //start_experiment(req_size, 6300, nb_max_req);
    //start_experiment(req_size, 4500, nb_max_req);
    start_experiment(req_size, 4300, nb_max_req);
  } else if (req_size == 4000) {
    //start_experiment(req_size, 5000, nb_max_req);
    //start_experiment(req_size, 3200, nb_max_req);
    start_experiment(req_size, 400, nb_max_req);
  } else {
    fprintf(stderr, "Manager dunno what to do with reqs of size %dB\n", req_size); 
  }

  // End, send final request
  ManReq req;
  req.type = mr_end;
  send_request(req);

  return 0;
}
