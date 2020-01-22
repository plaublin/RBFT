#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <netinet/tcp.h> // for TCP_NODELAY
#include <map>

#include "Wrapped_request.h"
#include "Request.h"
#include "Verifier_thread.h"
#include "Verifier.h"
#include "parameters.h"
#include "tcp_net.h"
#include "attacks.h"
#include "Node_blacklister.h"

void *Verifier_thread_startup(void *_tgtObject)
{
  Verifier_thread *tgtObject = (Verifier_thread *) _tgtObject;
  //  fprintf(stderr, "Running a verifier thread object in a new thread\n");
  void *threadResult = tgtObject->run();
  //  fprintf(stderr, "Deleting object\n");
  delete tgtObject;
  return threadResult;
}

void Verifier_thread::init_comm_with_clients(void) {
#if USE_TCP_CONNECTIONS
  // initialize, bind and set the client socket
  // (used to receive request from clients)
  // =================================================================
  // Initialization Clients ----> Verifier
  // =================================================================

  // 1) initialize the sockets for communications with clients
  replica->clients_sockets_fds = (int*) malloc(
      sizeof(int) * replica->num_clients());
  for (int i = 0; i < replica->num_clients(); i++)
  {
    replica->clients_sockets_fds[i] = -1;
  }

  replica->clients_bootstrap_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (replica->clients_bootstrap_socket == -1)
  {
    perror("Error while creating the socket! ");
    exit(errno);
  }

  // TCP NO DELAY
  int flag = 1;
  int result = setsockopt(replica->clients_bootstrap_socket, IPPROTO_TCP,
      TCP_NODELAY, (char*) &flag, sizeof(int));

  if (result == -1)
  {
    perror("Error while setting TCP NO DELAY! ");
  }

  // 2) bind on it
  replica->clients_bootstrap_sin.sin_addr.s_addr = htonl(INADDR_ANY);
  replica->clients_bootstrap_sin.sin_family = AF_INET;
  replica->clients_bootstrap_sin.sin_port = htons(
      replica->replicas_ports[replica->id()]);

  if (bind(replica->clients_bootstrap_socket,
      (struct sockaddr*) &replica->clients_bootstrap_sin,
      sizeof(replica->clients_bootstrap_sin)) == -1)
  {
    perror("Error while binding to the bootstrap clients socket! ");
    exit(errno);
  }

  // 3) make the socket listening for incoming connections
  if (listen(replica->clients_bootstrap_socket, replica->num_clients() + 1)
      == -1)
  {
    perror("Error while calling listen! ");
    exit(errno);
  }
#else
  replica->clients_sockets_fds = NULL;
  replica->clients_bootstrap_socket = -1;
#endif

  fprintf(stderr,
      "Verifier thread: Sockets for communications with clients initialized!\n");
}

void Verifier_thread::handle(Wrapped_request* m)
{
  Request* req;

#ifdef DEBUG_PERIODICALLY_PRINT_THR
  __sync_fetch_and_add (&replica->nb_received_requests, 1);
#endif

  //Sonia: HERE IS ANOTHER HACK: on appelle la méthode verify pour avoir le cout de la vérification, mais on utilise le boolean is_mac_valid
  bool mac_is_valid = Message::is_mac_valid(
      (Message_rep*) ((Wrapped_request*) m->contents()));

#ifdef MSG_DEBUG
  //fprintf(stderr, "[Verifier Thread] Client %d sends a request with mac_is_valid=%s\n", ((Wrapped_request*)m)->client_id(), (mac_is_valid ? "true" : "false"));
#endif

  //Here is the real MAC + signature verification
  req = replica->verify((Wrapped_request*) m);

  // deactivating the client blacklister
  //client_blacklister->add_message(i, mac_is_valid);

  if (req && mac_is_valid)
  {
    Request_id rid = req->request_id();
    int cid = req->client_id() - replica->num_replicas;

#ifdef MSG_DEBUG
    fprintf(stderr, "Verifier_thread: has received request (%d, %qd)\n", req->client_id(), rid);
#endif

    nb_valid++;

    pthread_mutex_lock(&replica->missing_reqs_lock[cid]);

    if (rid > replica->max_verified_rid[cid] && rid - replica->min_verified_rid[cid]
        < MISSING_RID_MAX_RANGE)
    {
      bool ret = replica->verifier_thr_to_fwd_thr_buffer->cb_write_msg(
          (Message*) req);
      if (!ret)
      {
#ifdef MSG_DEBUG
        fprintf(stderr, "Verifier thread: error with the Circular buffer for request (%d, %qd)\n", cid, rid);
#endif
        delete req;
      }
      else
      {
#ifdef MSG_DEBUG
    fprintf(stderr,
        "[Verifier Thread]: Received request %qd from client %i. Sending to forwarder\n",
        rid, cid);
    fprintf(stderr, "[Verifier Thread]: Adding [%qd, %qd] to replica->missing_reqs[%d], min[%d]=%qd, max[%d]=%qd\n", replica->max_verified_rid[cid]+1, rid-1, cid, cid, replica->min_verified_rid[cid], cid, replica->max_verified_rid[cid]);
#endif

#ifdef DEBUG_PERIODICALLY_PRINT_THR
          __sync_fetch_and_add (&replica->nb_sent_requests_to_forwarder, 1);
#endif

        for (Request_id i = replica->max_verified_rid[cid] + 1; i < rid; i++)
        {
          replica->missing_reqs[cid][i] = 1;
        }
        replica->max_verified_rid[cid] = rid;
        if (replica->min_verified_rid[cid] + 1 == rid)
        {
          replica->min_verified_rid[cid] = rid;
        }

#ifdef MSG_DEBUG
       fprintf(stderr, "[Verifier Thread]: Now, min[%d]=%qd, max[%d]=%qd\n", cid, replica->min_verified_rid[cid], cid, replica->max_verified_rid[cid]);
#endif
      }
    }
    else
    {
      std::map<Request_id, char>::iterator got =
          replica->missing_reqs[cid].find(rid);
      if (got != replica->missing_reqs[cid].end())
      {
        bool ret = replica->verifier_thr_to_fwd_thr_buffer->cb_write_msg(
            (Message*) req);
        if (!ret)
        {
#ifdef MSG_DEBUG
          fprintf(stderr, "Verifier thread: error with the Circular buffer for request (%d, %qd)\n", req->client_id(), req->request_id());
#endif
          delete req;
        }
        else
        {
#ifdef MSG_DEBUG
    fprintf(stderr,
        "[Verifier Thread]: Received request %qd from client %i. Sending to forwarder\n",
        rid, req->client_id());
    fprintf(stderr, "[Verifier Thread]: Remove the request from the missing ones; min=%qd\n", replica->min_verified_rid[cid]);
#endif

#ifdef DEBUG_PERIODICALLY_PRINT_THR
          __sync_fetch_and_add (&replica->nb_sent_requests_to_forwarder, 1);
#endif

          replica->missing_reqs[cid].erase(got);
          if (replica->missing_reqs[cid].empty()) {
            replica->min_verified_rid[cid] = replica->max_verified_rid[cid];
          } else {
            replica->min_verified_rid[cid] = replica->missing_reqs[cid].begin()->first-1;
          }

#ifdef MSG_DEBUG
    fprintf(stderr, "[Verifier Thread]: Now min[%d]=%qd\n", cid, replica->min_verified_rid[cid]);
#endif
        }
      }
      else if (rid <= replica->max_verified_rid[cid])
      {
#ifdef MSG_DEBUG
    fprintf(stderr,
        "[Verifier Thread]: Received request %qd from client %i. Ask for a retransmission\n",
        rid, req->client_id());
#endif

        // retrans
        replica->verifier_to_executor_buffer->bcb_write_msg((Message*) req);
      }
      else
      {
#ifdef MSG_DEBUG
        fprintf(stderr, "Verifier_thread: cannot consider request (%d, %qd): min_verified_rid[%d]=%qd\n", req->client_id(), rid, cid, replica->min_verified_rid[cid]);
#endif
        delete req;
      }
    }

    pthread_mutex_unlock(&replica->missing_reqs_lock[cid]);
  }
  else
  {
#ifdef MSG_DEBUG
    fprintf(stderr,"Req is not valid\n");
#endif

    // mac is not valid
    nb_non_valid++;
    delete req;
  }

  unsigned long sum = nb_non_valid + nb_valid;
  if (sum == PERIODIC_DISPLAY_PERC_VALID)
  {
    fprintf(
        stderr,
        "nb requests %lu, nb_valid %lu, nb_non_valid %lu, percentage valid %f %%\n",
        sum, nb_valid, nb_non_valid, nb_valid * 100.0 / sum);
    nb_valid = 0;
    nb_non_valid = 0;
  }
}

void *Verifier_thread::run(void)
{
  fd_set file_descriptors; //set of file descriptors to listen to (only one socket, in this case)
  timeval listen_time; //max time to wait for something readable in the file descriptors

  while (!replica)
  {
    fprintf(stderr, "replica not initialized yet...\n");
    sleep(1);
  }
  fprintf(stderr, "Verifier thread running!\n");

  replica->min_verified_rid = new Request_id[replica->num_clients()];
  replica->max_verified_rid = new Request_id[replica->num_clients()];
  replica->missing_reqs = new std::map<Request_id, char>[replica->num_clients()];
  for (int i = 0; i < replica->num_clients(); i++)
  {
    replica->min_verified_rid[i] = 0;
    replica->max_verified_rid[i] = 0;
  }

  init_comm_with_clients();

  client_blacklister = new Node_blacklister(replica->num_clients(),
      NODE_BLACKLISTER_SLIDING_WINDOW_SIZE,
      READING_FROM_BLACKLISTED_NODES_PERIOD);

  nb_valid = 0;
  nb_non_valid = 0;

  Message* m;

  //Initialize some date strucutures
  int maxsock;
  int rcv_client_sock;

  while (1)
  {
    if (replica->excluded_clients)
    {
      continue;
    }

    FD_ZERO(&file_descriptors); //initialize file descriptor set
    maxsock = 0;

#if USE_TCP_CONNECTIONS
    maxsock = replica->clients_bootstrap_socket;
    FD_SET(replica->clients_bootstrap_socket, &file_descriptors);
    for (int i = 0; i < replica->num_clients(); i++)
    {
      if (replica->clients_sockets_fds[i] != -1
          && (!client_blacklister->is_blacklisted(i)
              || client_blacklister->time_to_read_from_blacklisted()))
      {
        FD_SET(replica->clients_sockets_fds[i], &file_descriptors);
        maxsock = MAX(maxsock, replica->clients_sockets_fds[i]);
      }
    }
#else
    maxsock = MAX(maxsock, replica->sock);
    FD_SET(replica->sock, &file_descriptors);
#endif

    listen_time.tv_sec = 0;
    listen_time.tv_usec = 500;

    select(maxsock + 1, &file_descriptors, NULL, NULL, &listen_time);

#if USE_TCP_CONNECTIONS
    /* get a new connection from a client */
    if (FD_ISSET(replica->clients_bootstrap_socket, &file_descriptors))
    {
      // accept the socket
      sockaddr_in csin;
      int sinsize = sizeof(csin);
      int current_client_socket_fd = accept(replica->clients_bootstrap_socket,
          (struct sockaddr*) &csin, (socklen_t*) &sinsize);

      if (current_client_socket_fd == -1)
      {
        perror("An invalid socket has been accepted: ");
      }
      else
      {
        //TCP NO DELAY
        int flag = 1;
        int result = setsockopt(current_client_socket_fd, IPPROTO_TCP,
            TCP_NODELAY, (char *) &flag, sizeof(int));

        if (result == -1)
        {
          perror("Error while setting TCP NO DELAY on client socket! ");
        }

        int client_port = ntohs(csin.sin_port);
        char *hostname = inet_ntoa(csin.sin_addr);

        int i = 0;
        while (i < replica->num_clients())
        {
          //fprintf(stderr, "Client %i is %s:%i\n", i, clients_ipaddr[i],
          //    clients_ports[i]);
          if (client_port == replica->clients_ports[i] && !strcmp(hostname,
              replica->clients_ipaddr[i]))
          {
            replica->clients_sockets_fds[i] = current_client_socket_fd;
            break;
          }
          i++;
        }

        fprintf(stderr,
            "Received a new connection from a client: %s:%i (socket %d). Should be client %i\n",
            hostname, client_port, current_client_socket_fd, i);

        if (i >= replica->num_clients())
        {
          fprintf(stderr, "Error: This client is unknown\n");
        }
        else
        {
          // Re-enter the top level while(1) loop from the beginning.
          continue;
        }
      }
    } // end else IF_ISSET
#endif

    // ------------------------------------------------------------
    // Below handle the case of a message received from a client.
    // ------------------------------------------------------------

#if USE_TCP_CONNECTIONS
    // is there a message from somebody?
    rcv_client_sock = -1;
    for (int i = 0; i < replica->num_clients(); i++)
    {
      // yes there is: receive it and then process it
      if (replica->clients_sockets_fds[i] != -1
          && FD_ISSET(replica->clients_sockets_fds[i], &file_descriptors))
      {
        rcv_client_sock = replica->clients_sockets_fds[i];

#else
      if (FD_ISSET(replica->sock, &file_descriptors)) {
        rcv_client_sock = replica->sock;
#endif

        m = new Message(Max_message_size);

#if USE_TCP_CONNECTIONS
        // 1) first of all, we need to receive the Message_rep (in order to get the message size)
        int msg_rep_size = recvMsg(rcv_client_sock, (void*) m->contents(),
            sizeof(Message_rep));

        // 2) now that we have the size of the message, receive the content
        int msg_size = recvMsg(rcv_client_sock,
            (void*) ((char*) m->contents() + msg_rep_size),
            m->size() - msg_rep_size);

        int ret = msg_rep_size + msg_size;
#else
        int ret = recvfrom(replica->sock, m->contents(), m->msize(), 0, 0, 0);
#endif

        if (ret >= (int) sizeof(Message_rep) && ret >= m->size())
        {
          if (m->tag() == Wrapped_request_tag)
          {
            handle((Wrapped_request*)m);
          }
          else if (m->tag() == New_key_tag)
          {
#ifdef MSG_DEBUG
            fprintf(stderr, "Replica %i, View %qd, received a New_key msg.\n", replica->id(),
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
                replica->id(), m->tag());
#endif
            delete m;
          }
        }
        else
        {
#ifdef MSG_DEBUG
          fprintf(stderr, "Size not valid: %d >= %d && %d >= %d\n",
              ret, (int)sizeof(Message_rep), ret, m->size());
#endif
          delete m;
        }
      }
#if USE_TCP_CONNECTIONS
    }
#endif

    client_blacklister->new_reading_iteration();
  }

  return NULL;
}
