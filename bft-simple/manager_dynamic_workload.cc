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

// vector of the number of clients for each burst
vector<int> nb_clients_for_bursts;

// vector of the bursts size for each burst
vector<int> burst_size_for_bursts;

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
int timeout = 120;
int req_size = 8;
int rep_size = 8;
unsigned long total_nb_req = 0;

// to compute the throughput on the whole experiment
long long exp_start_time, exp_end_time;

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

void read_xp_file(char *xp_file)
{
  // read the file
  FILE *F = fopen(xp_file, "r");
  if (F == NULL)
  {
    fprintf(stderr, "Error while trying to open %s. Is the file existing?\n",
        xp_file);
    exit(-1);
  }

  if (fscanf(F, "%d", &req_size) == EOF)
  {
    fprintf(stderr, "Error while reading the file at the 1st line\n");
    exit(-1);
  }
  rep_size = req_size;

  int v, b;
  while (fscanf(F, "%d %d", &v, &b) != EOF)
  {
    nb_clients_for_bursts.push_back(v);
    burst_size_for_bursts.push_back(b);
  }

  fclose(F);

  /*
   cout << "The contents are:";
   for (int i=0; i < vec->size(); i++)
   cout << " " << vec->at(i);
   cout << endl;
   */
}

/*
 * initialize everything (basically sockets)
 */
int init_connection_with_clients(void)
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
  int result = setsockopt(listen_clients_socket_fd, IPPROTO_TCP, TCP_NODELAY,
      (char*) &flag, sizeof(int));

  if (result == -1)
  {
    fprintf(
        stderr,
        "Unable to set TCP_NODELAY for socket listening for clients connections, exiting.\n");
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
  }
  else

    fprintf(stderr,
        "Manager successfully bound listening socket on port: %i\n",
        MANAGER_PORT);

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
        (struct sockaddr*) &csin, (socklen_t*) &sinsize);

    if (accepted_client_sockets_fd[i] == -1)
    {
      perror("An invalid socket has been accepted! ");
      continue;
    }

    // TCP NO DELAY
    flag = 1;
    int result = setsockopt(accepted_client_sockets_fd[i], IPPROTO_TCP,
        TCP_NODELAY, (char*) &flag, sizeof(int));

    if (result == -1)
    {
      fprintf(stderr,
          "Unable to set TCP_NODELAY on accepted connection from client, exiting.\n");
      exit(-1);
    }

    // add the socket to the array of sockets
#ifdef DEBUG
    fprintf(stderr, "A connection has been accepted from client %s:%i\n",
        inet_ntoa(csin.sin_addr), ntohs(csin.sin_port));
#endif
  }

  return 0;
}

/*
 * function to send ONE request to nb_clients clients
 */
int send_request(ManReq req, int nb_clients)
{
#ifdef DEBUG
  //fprintf(stderr, "Sending request:\n\ttype = %i\n\tnb requests = %i\n\trequests size = %i\n",
  //    req.type, req.nb_requests, req.size);
#endif

  // sending req to each client
  for (int c = 0; c < nb_clients; c++)
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
      max
          = ((accepted_client_sockets_fd[i] > max) ? accepted_client_sockets_fd[i]
              : max);
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

void open_manager_log()
{
  if ((F = fopen("manager.log", "w")) != NULL)
  {
    fprintf(F, "#num_clients=%i\n#req_size=%i\n#rep_size=%i\n", num_clients,
        req_size, rep_size);
    fprintf(F,
        "#total_nb_req\tnb_clients\tcurrent_throughput\tcurrent_latency\n");
    fflush(F);
  }

  fprintf(stderr,
      "#num_clients=%i\n#req_size=%i\n#rep_size=%i\n",
      num_clients, req_size, rep_size);
  fprintf(stderr,
      "#total_nb_req\tnb_clients\tcurrent_throughput\tcurrent_latency\n");

  ManReq req;
  req.type = mr_ready;

  // send the ready requests, for the clients to save the start time
  send_request(req, num_clients);

  // start time
  exp_start_time = currentTime();
}

void close_manager_log()
{
  // stop time
  exp_end_time = currentTime();

  // compute throughput
  float thr = total_nb_req / float(diffTime(exp_end_time, exp_start_time))
      * 1000000.0;

  if (F)
  {
    fprintf(F, "#thr on whole exp = %f req/s\n", thr);
  }
  fprintf(stderr, "#thr on whole exp = %f req/s\n", thr);

  fclose(F);
}

/* launch experiments */
void start_experiment(int size, float limit_thr, int nbc, int burst_size)
{
  ManReq req;
  float current_thr, current_lat;

  req.type = mr_burst;
  req.nb_requests = burst_size;
  req.size = req_size;
  req.limit_thr = ceil((double) limit_thr / (double) nbc);
  req.start_logging = 1;

  // send the start burst, for the clients to start
  send_request(req, nbc);

  ManReq* results_from_clients = (ManReq*) malloc(sizeof(ManReq) * num_clients);

  for (int i = 0; i < num_clients; i++)
  {
    results_from_clients[i].client_id = -1;
    results_from_clients[i].latency_mean = 0;
  }

  int nb_messages_received = 0;
  while (nb_messages_received < nbc)
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

  // get stats
  for (int i = 0; i < num_clients; i++)
  {
    //fprintf(stderr,"Client %d, lat= %f, thr= %f\n",i,results_from_clients[i].latency_mean,results_from_clients[i].limit_thr);
    if (results_from_clients[i].latency_mean > 0)
    {
      num_correct_clients++;
      total_nb_req += results_from_clients[i].nb_requests;
      current_lat += results_from_clients[i].latency_mean;
      current_thr += results_from_clients[i].limit_thr;
    }
  }

  // num_correct_clients is the number of clients from which we have received a response. Should be equal to nbc
  if (num_correct_clients != nbc)
  {
    fprintf(stderr, "I expect %d clients, not %d\n", nbc, num_correct_clients);
  }

  //we consider now only results coming from correct clients
  current_lat /= (float) num_correct_clients;

  fprintf(stderr, "%lu\t%d\t%f\t%f\n", total_nb_req, nbc, current_thr,
      current_lat);
  if (F != NULL)
  {
    fprintf(F, "%lu\t%d\t%f\t%f\n", total_nb_req, nbc, current_thr, current_lat);
    fflush(F);
  }

  next_xp = 0;
}

/*
 * main ;)
 */
int main(int argc, char **argv)
{
  char xp_file[PATH_MAX];
  xp_file[0] = 0;

  // add signal
  signal(SIGINT, next_xp_fn);

  // process command line options
  int opt;
  while ((opt = getopt(argc, argv, "f:c:x:")) != EOF)
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

    case 'x':
      strncpy(xp_file, optarg, PATH_MAX);
      xp_file[PATH_MAX] = 0;
      break;

    default:
      fprintf(stderr, "%s -f nb_faults -c nb_clients -x xp_file\n", argv[0]);
      exit(-1);
    }
  }

  // initializations
  read_xp_file(xp_file);

  fprintf(
      stderr,
      "Launching manager...\n\tnb faults: %i\n\tnb clients: %i\nreq_size: %i\nxp_file: %s\n",
      num_faults, num_clients, req_size, xp_file);

  init_clock_mhz();
  init_connection_with_clients();

  open_manager_log();

  // according to the xp_file, send the bursts to the given nb of clients
  for (unsigned int i = 0; i < nb_clients_for_bursts.size(); i++)
  {
    //start_experiment(req_size, 70000, nb_clients_for_bursts[i], burst_size_for_bursts[i]);

    //open loop: limit the throughput
    if (req_size == 8)
      start_experiment(req_size, 300*nb_clients_for_bursts[i], nb_clients_for_bursts[i], burst_size_for_bursts[i]);
    else if (req_size == 100)
      start_experiment(req_size, 652*nb_clients_for_bursts[i], nb_clients_for_bursts[i], burst_size_for_bursts[i]);
    else if (req_size == 500)
      start_experiment(req_size, 799*nb_clients_for_bursts[i], nb_clients_for_bursts[i], burst_size_for_bursts[i]);
    else if (req_size == 1000)
      start_experiment(req_size, 438*nb_clients_for_bursts[i], nb_clients_for_bursts[i], burst_size_for_bursts[i]);
    else if (req_size == 2000)
      start_experiment(req_size, 191*nb_clients_for_bursts[i], nb_clients_for_bursts[i], burst_size_for_bursts[i]);
    else if (req_size == 3000)
      start_experiment(req_size, 207*nb_clients_for_bursts[i], nb_clients_for_bursts[i], burst_size_for_bursts[i]);
    else if (req_size == 4000)
      start_experiment(req_size, 400*nb_clients_for_bursts[i], nb_clients_for_bursts[i], burst_size_for_bursts[i]);
    else
      fprintf(stderr, "Req size error: %d\n", req_size);
  }

  close_manager_log();

  // End, send final request
  ManReq req;
  req.type = mr_end;
  send_request(req, num_clients);

  return 0;
}
