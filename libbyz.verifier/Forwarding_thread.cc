#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <netinet/tcp.h> // for TCP_NODELAY
#include "Wrapped_request.h"
#include "Request.h"
#include "Forwarding_thread.h"
#include "Verifier.h"
#include "parameters.h"
#include "tcp_net.h"
#include "Propagate.h"
#include "attacks.h"
#include "Protocol_instance_change.h"

void *Forwarding_thread_startup(void *_tgtObject)
{
  Forwarding_thread *tgtObject = (Forwarding_thread *) _tgtObject;
  fprintf(stderr, "Running a forwarding thread object in a new thread\n");
  void *threadResult = tgtObject->run();
  fprintf(stderr, "Deleting forwarding thread object\n");
  delete tgtObject;
  return threadResult;
}

void *Forwarding_thread::run(void)
{
  fd_set file_descriptors; //set of file descriptors to listen to (only one socket, in this case)
  timeval listen_time; //max time to wait for something readable in the file descriptors

  c_retrans = 0;

  while (!replica)
  {
    fprintf(stderr, "[FWD_thread]: replica not initialized yet...\n");
    sleep(1);
  }
  fprintf(stderr, "Forwarding thread running!\n");

  //Note: the creation of the connections is done by the Verifier.cc

#ifndef ATTACK2
  if (floodMax())
  {
    fprintf(stderr, "Node %d runs flood attack\n", replica->id());
    run_flood_attack();
  }
#endif

#if USE_GETTIMEOFDAY
  retrans_period.tv_sec = FORWARDER_RETRANS_TIMEOUT/1000;
  retrans_period.tv_usec =  (FORWARDER_RETRANS_TIMEOUT%1000)*1000;
#else
  retrans_period = FORWARDER_RETRANS_TIMEOUT*clock_mhz*1000;
#endif
  retrans_deadline = currentTime() + retrans_period;

  // load balancing
#ifdef LOAD_BALANCING_SEND
  send_to_master_nic = new int[replica->num_replicas];
  for (int i=0; i<replica->num_replicas; i++) {
    send_to_master_nic[i] = 0;
  }
#endif

  node_blacklister = new Node_blacklister(replica->num_replicas,
      NODE_BLACKLISTER_SLIDING_WINDOW_SIZE,
      READING_FROM_BLACKLISTED_NODES_PERIOD);

  last_propagate_send = currentTime();
  zepropagate = new Propagate();

  Message* mp;
  int maxsock;

  /* flooding protection */
  int *nb_msg = new int[replica->num_replicas + 1];
  int max_replica_messages;
  int max_replica_messages_index;
  int second_max_replica_messages;
  int second_max_replica_messages_index;

  for (int i = 0; i <= replica->num_replicas; i++)
  {
    nb_msg[i] = 0;
  }

  while (1)
  {
    FD_ZERO(&file_descriptors); //initialize file descriptor set
    maxsock = 0;

    //SONIA: Adding other the file descriptors of the other verifiers
    for (int i = 0; i < replica->num_replicas; i++)
    {
      if (replica->rcv_socks[i] != -1 && (!node_blacklister->is_blacklisted(i)
          || node_blacklister->time_to_read_from_blacklisted()))
      {
        FD_SET(replica->rcv_socks[i], &file_descriptors);
        maxsock = MAX(maxsock, replica->rcv_socks[i]);
      }
      if (replica->rcv_socks_lb[i] != -1 && (!node_blacklister->is_blacklisted(i)
          || node_blacklister->time_to_read_from_blacklisted()))
      {
        FD_SET(replica->rcv_socks_lb[i], &file_descriptors);
        maxsock = MAX(maxsock, replica->rcv_socks_lb[i]);
      }
    }
    FD_SET(replica->verifier_thr_to_fwd_thr_buffer->fd,&file_descriptors);
    maxsock = MAX(maxsock, replica->verifier_thr_to_fwd_thr_buffer->fd);

    listen_time.tv_sec = 0;
    listen_time.tv_usec = 500;

    select(maxsock + 1, &file_descriptors, NULL, NULL, &listen_time);

    exec_retrans_timer();

    send_propagate_if_needed();

    /*****************************************************************/
    //Is there a request to forward?
    if (FD_ISSET(replica->verifier_thr_to_fwd_thr_buffer->fd, &file_descriptors))
    {
      mp = replica->verifier_thr_to_fwd_thr_buffer->cb_read_msg();
      if (mp && mp != replica->verifier_thr_to_fwd_thr_buffer->cb_magic())
      {
#ifdef DEBUG_PERIODICALLY_PRINT_THR
        __sync_fetch_and_add (&replica->nb_recv_requests_from_verifier_thr, 1);
#endif

        handle((Request*) mp);
      }
    }
    /*****************************************************************/

    //SONIA: listening on the sockets of other verifiers
    for (int x = 0; x < replica->num_replicas; x++)
    {
      //I want to listen at this replica
      if (FD_ISSET(replica->rcv_socks[x], &file_descriptors))
      {
        nb_msg[x]++;
        nb_msg[replica->num_replicas]++;

#ifdef DEBUG_PERIODICALLY_PRINT_THR
          __sync_fetch_and_add (&replica->nb_recv_propagate[x], 1);
#endif

        mp = get_message_from_forwarder(replica->rcv_socks[x]);
        if (mp)
        {
#ifdef MSG_DEBUG
          //fprintf(stderr, "I am the forwarding thread and I have received a message whose tag is %d\n", mp->tag());
#endif
          if (mp->tag() == Propagate_tag)
          {
            handle((Propagate*) mp, x);
            delete mp;
          }
          else if (mp->tag() == Protocol_instance_change_tag)
          {
            Protocol_instance_change *pic = (Protocol_instance_change*)mp;
            if (replica->verify_protocol_instance_change(pic))
            {
              // forward it to the Verifier
#ifdef MSG_DEBUG
              fprintf(stderr,
                  "I have received a valid Protocol_instance_change message. I need to forward it to the Verifier.\n");
#endif
              replica->request_buffer->cb_write_msg((Message*) mp);
              node_blacklister->add_message(x, true);
            }
            else
            {
              node_blacklister->add_message(x, false);
              delete mp;
            }
          }
          else
          {
            fprintf(
                stderr,
                "Forwarding thread received a message with tag %i from forwarder %i\n",
                mp->tag(), x);
            delete mp;
          }
        }

        if (nb_msg[replica->num_replicas] > check_nb_msg)
        {

          //time to check. compute max and second_max
          //fprintf(stderr, "Replica %i: %d %d %d %d %d\n", id(), nb_msg[0], nb_msg[1], nb_msg[2], nb_msg[3], nb_msg[replica->num_replicas]);
          max_replica_messages = -1;
          max_replica_messages_index = -1;
          second_max_replica_messages = -1;
          second_max_replica_messages_index = -1;

          // get the first max
          for (int k = 0; k < replica->num_replicas; k++)
          {
            if (nb_msg[k] > max_replica_messages)
            {
              max_replica_messages = nb_msg[k];
              max_replica_messages_index = k;
            }
          }

          // get the second max
          for (int k = 0; k < replica->num_replicas; k++)
          {
            if (nb_msg[k] != max_replica_messages && nb_msg[k]
                > second_max_replica_messages)
            {
              second_max_replica_messages = nb_msg[k];
              second_max_replica_messages_index = k;
            }
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
             node_blacklister->blacklist_node(max_replica_messages_index);
             */
          }

          for (int j = 0; j <= replica->num_replicas; j++)
            nb_msg[j] = 0;
        }
      } // end FD_ISSET

      //I want to listen at this replica
      if (FD_ISSET(replica->rcv_socks_lb[x], &file_descriptors))
      {
        nb_msg[x]++;
        nb_msg[replica->num_replicas]++;

#ifdef DEBUG_PERIODICALLY_PRINT_THR
          __sync_fetch_and_add (&replica->nb_recv_propagate[x], 1);
#endif

        mp = get_message_from_forwarder(replica->rcv_socks_lb[x]);
        if (mp)
        {
#ifdef MSG_DEBUG
          fprintf(stderr, "I am the forwarding thread and I have received a message whose tag is %d\n", mp->tag());
#endif
          if (mp->tag() == Propagate_tag)
          {
            handle((Propagate*) mp, x);
            delete mp;
          }
          else if (mp->tag() == Protocol_instance_change_tag)
          {
            Protocol_instance_change *pic = (Protocol_instance_change*)mp;
            if (replica->verify_protocol_instance_change(pic))
            {
              // forward it to the Verifier
#ifdef MSG_DEBUG
              fprintf(stderr,
                  "I have received a valid Protocol_instance_change message. I need to forward it to the Verifier.\n");
#endif
              replica->request_buffer->cb_write_msg((Message*) mp);
              node_blacklister->add_message(x, true);
            }
            else
            {
              node_blacklister->add_message(x, false);
              delete mp;
            }
          }
          else
          {
            fprintf(
                stderr,
                "Forwarding thread received a message with tag %i from forwarder %i\n",
                mp->tag(), x);
            delete mp;
          }
        }

        if (nb_msg[replica->num_replicas] > check_nb_msg)
        {

          //time to check. compute max and second_max
          //fprintf(stderr, "Replica %i: %d %d %d %d %d\n", id(), nb_msg[0], nb_msg[1], nb_msg[2], nb_msg[3], nb_msg[replica->num_replicas]);
          max_replica_messages = -1;
          max_replica_messages_index = -1;
          second_max_replica_messages = -1;
          second_max_replica_messages_index = -1;

          // get the first max
          for (int k = 0; k < replica->num_replicas; k++)
          {
            if (nb_msg[k] > max_replica_messages)
            {
              max_replica_messages = nb_msg[k];
              max_replica_messages_index = k;
            }
          }

          // get the second max
          for (int k = 0; k < replica->num_replicas; k++)
          {
            if (nb_msg[k] != max_replica_messages && nb_msg[k]
                > second_max_replica_messages)
            {
              second_max_replica_messages = nb_msg[k];
              second_max_replica_messages_index = k;
            }
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
             node_blacklister->blacklist_node(max_replica_messages_index);
             */
          }

          for (int j = 0; j <= replica->num_replicas; j++)
            nb_msg[j] = 0;
        }
      } // end FD_ISSET
    } // end for each verifier

    node_blacklister->new_reading_iteration();
  }

  return NULL;
}

void Forwarding_thread::run_flood_attack(void)
{
  Message *m1, *m2;
  int maxsock;
  fd_set file_descriptors; //set of file descriptors to listen to (only one socket, in this case)
  timeval listen_time; //max time to wait for something readable in the file descriptors

  m2 = new Message(Propagate_tag, Max_message_size);
  Message::set_mac_unvalid((Message_rep*) m2->contents());

  while (1)
  {
    //recv messages because we use TCP
    FD_ZERO(&file_descriptors); //initialize file descriptor set
    FD_SET(replica->verifier_thr_to_fwd_thr_buffer->fd, &file_descriptors);
    maxsock = replica->verifier_thr_to_fwd_thr_buffer->fd;

    //SONIA: Adding other the file descriptors of the other verifiers
    for (int i = 0; i < replica->num_replicas; i++)
    {
      FD_SET(replica->rcv_socks[i], &file_descriptors);
      maxsock = MAX(maxsock, replica->rcv_socks[i]);
      FD_SET(replica->rcv_socks_lb[i], &file_descriptors);
      maxsock = MAX(maxsock, replica->rcv_socks_lb[i]);
    }

    listen_time.tv_sec = 0;
    listen_time.tv_usec = 500;

    select(maxsock + 1, &file_descriptors, NULL, NULL, &listen_time);

    if (FD_ISSET(replica->verifier_thr_to_fwd_thr_buffer->fd, &file_descriptors))
    {
      m1 = replica->verifier_thr_to_fwd_thr_buffer->cb_read_msg();
      if (m1 && m1 != replica->verifier_thr_to_fwd_thr_buffer->cb_magic())
      {
        delete m1;
      }
    } 

    //SONIA: listening on the sockets of other verifiers
    for (int x = 0; x < replica->num_replicas; x++)
    {
      //I want to listen at this replica
      if (FD_ISSET(replica->rcv_socks[x], &file_descriptors))
      {
        m1 = get_message_from_forwarder(replica->rcv_socks[x]);
        if (m1)
          delete m1;

        // flood it (it will not block as we have received a message)
        //send an unvalid Propagate of max size to all nodes
#if defined(ATTACK1)
        // flood only the node on which runs the primary of the master instance
        if (x == 0)
        {
#endif
         //load_balancing_send((Message *) m2, x);
#ifdef ATTACK1
      }
#endif
      }

      if (FD_ISSET(replica->rcv_socks_lb[x], &file_descriptors))
      {
        m1 = get_message_from_forwarder(replica->rcv_socks_lb[x]);
        if (m1)
          delete m1;

        // flood it (it will not block as we have received a message)
        //send an unvalid Propagate of max size to all nodes
#if defined(ATTACK1)
        // flood only the node on which runs the primary of the master instance
        if (x == 0)
        {
#endif
          //load_balancing_send((Message *) m2, x);
#ifdef ATTACK1
        }
#endif
      }
    } // end for each verifier
  }
}

// Read a message from socket sock and return it (or 0 in case of error).
Message *Forwarding_thread::get_message_from_forwarder(int sock)
{
  Message *mp = new Message(Max_message_size);

#if USE_TCP_CONNECTIONS
  //  replica->something_to_read_fwd++;
  // 1) first of all, we need to receive the Message_rep (in order to get the message size)
  int msg_rep_size = recvMsg(sock, (void*) mp->contents(), sizeof(Message_rep));

  // 2) now that we have the size of the message, receive the content
  int msg_size = recvMsg(sock, (void*) ((char*) mp->contents() + msg_rep_size),
      mp->size() - msg_rep_size);

  int ret = msg_rep_size + msg_size;
#else
  int ret = recvfrom(sock, mp->contents(), mp->msize(), 0, 0, 0);
#endif

  if (ret >= (int) sizeof(Message_rep) && ret >= mp->size())
  {
    return mp;
  }
  else
  {
    // fprintf(stderr, "--------- WTF? --------- ret: %d from replica %d\n",ret,j);
    if (ret < 0)
      fprintf(stderr, "errno: %s\n", strerror(errno));

    delete mp;
    return 0;
  }
}

// req is a valid request
void Forwarding_thread::handle(Request *req)
{
  Request_id rid;
  int cid, idx1;

  rid = req->request_id();
  cid = req->client_id();
  idx1 = cid - replica->num_replicas;

#ifdef MSG_DEBUG
  fprintf(stderr,
    "[FWD Thread]:Received request from the verifier thread: client %i, id %qd\n", cid, rid);
#endif

  if (idx1 < 0 || idx1 > replica->num_clients())
  {
    fprintf(stderr, "[FWD thread] I handle a request of bad client %i\n", cid);
    delete req;
    return;
  }

#ifdef ATTACK2
  if (floodMax()) {
    // send propagate to the other nodes (they receive an invalid request)
    Propagate *pr = new Propagate();
    pr->add_request(req);
    pr->trim();
    pr->authenticate();
    send_propagate(pr);
    delete pr;

    // send request to the verifier: we do not wait for the propagate phase to complete
    req->rep().replier = -1;
    replica->request_buffer->cb_write_msg((Message*) req);
  } else {
#endif

      // needed for the MAC verification
      req->rep().replier = replica->id();
      add_request_to_certificate(req, replica->id());

#ifdef ATTACK2
  }
#endif
}

// pr is a valid propagate. pr is deleted by the caller of this method.
void Forwarding_thread::handle(Propagate *pr, int src)
{
#ifdef MSG_DEBUG
    //fprintf(stderr, "[FWD thread] I handle a Propagate from %d\n", src);
#endif

#ifdef ATTACK2
    if (floodMax()) {
      return;
    }
#endif

  bool v = pr->verify();
  v = Message::is_mac_valid((Message_rep*) (pr->contents()));

  if (v)
  {
    Request *req = 0;
    Propagate::Requests_iter iterator(pr);
    while (iterator.get(&req))
    {
      Request_id rid = req->request_id();
      int cid = req->client_id();
      int idx1 = cid - replica->num_replicas;

      if (idx1 < 0 || idx1 > replica->num_clients())
      {
#ifdef MSG_DEBUG
        fprintf(stderr,
            "[FWD thread] I handle a request in a Propagate of bad client %i\n",
            cid);
#endif
        delete req;
        return;
      }

      if (update_missing_reqs(cid, rid))
      {
        add_request_to_certificate(req, src);
      } else {
        delete req;
      }
    }
  }
  else
  {
#ifdef MSG_DEBUG
    fprintf(
        stderr,
        "[FWD thread][handle(Propagate*)] MAC is not valid in Propagate* from %i\n",
        src);
#endif
  }

  node_blacklister->add_message(src, v);
}

// update the missing_reqs structure, shared with the verifier thread
bool Forwarding_thread::update_missing_reqs(int cid, Request_id rid) {
  bool ret = true;
  int idx = cid - replica->num_replicas;

  pthread_mutex_lock(&replica->missing_reqs_lock[idx]);

  if (rid > replica->max_verified_rid[idx] && rid
      - replica->min_verified_rid[idx] < MISSING_RID_MAX_RANGE)
  {
#ifdef MSG_DEBUG
   fprintf(stderr,
        "[Forwarder Thread]: Received request %qd from client %i from forwarder\n",
        rid, cid);
    fprintf(stderr, "[Forwarder Thread]: Adding [%qd, %qd] to replica->missing_reqs[%d], min[%d]=%qd, max[%d]=%qd\n",
        replica->max_verified_rid[idx]+1, rid-1, idx, idx, replica->min_verified_rid[idx], idx, replica->max_verified_rid[idx]);
#endif

    for (Request_id i = replica->max_verified_rid[idx] + 1; i < rid; i++)
    {
      replica->missing_reqs[idx][i] = 1;
    }
    replica->max_verified_rid[idx] = rid;
    if (replica->min_verified_rid[idx] + 1 == rid)
    {
      replica->min_verified_rid[idx] = rid;
    }

#ifdef MSG_DEBUG
    fprintf(stderr, "[Forwarder Thread]: Now, min[%d]=%qd, max[%d]=%qd\n", idx, replica->min_verified_rid[idx], idx, replica->max_verified_rid[idx]);
#endif
  }
  else
  {
    std::map<Request_id, char>::iterator got =
        replica->missing_reqs[idx].find(rid);
    if (got != replica->missing_reqs[idx].end())
    {
#ifdef MSG_DEBUG
      fprintf(stderr, "[Forwarder Thread]: Remove the request (%d, %qd) from forwarder from the missing ones; min[%d]=%qd\n", cid, rid, idx, replica->min_verified_rid[idx]);
#endif

      replica->missing_reqs[idx].erase(got);
      if (replica->missing_reqs[idx].empty()) {
        replica->min_verified_rid[idx] = replica->max_verified_rid[idx];
      } else {
        replica->min_verified_rid[idx] = replica->missing_reqs[idx].begin()->first-1;
      }

#ifdef MSG_DEBUG
      fprintf(stderr, "[Forwarder Thread]: Now min[%d]=%qd\n", idx, replica->min_verified_rid[idx]);
#endif
    }
    else if (rid <= replica->max_verified_rid[idx])
    {
#ifdef MSG_DEBUG
  fprintf(stderr,
      "[Forwarder Thread]: Received request %qd from client %i. Will not ask for a retransmission\n",
      rid, cid);
#endif
    }
    else
    {
#ifdef MSG_DEBUG
      fprintf(stderr, "Forwarder_thread: cannot consider request (%d, %qd): min_verified_rid[%d]=%qd\n", cid, rid, idx, replica->min_verified_rid[idx]);
#endif

      ret = false;
    }
  }

  pthread_mutex_unlock(&replica->missing_reqs_lock[idx]);

  return ret;
}

// add request req received from node node_id to the certificate cert.
// return true if a new certificate has been created, false otherwise
bool Forwarding_thread::add_request_to_certificate(Request *req, int node_id)
{
  Request_id rid = req->request_id();
  int cid = req->client_id();
  int idx = cid - replica->num_replicas;
  bool ret = false;

  // this is an old request that has already been executed
  pthread_mutex_lock(&replica->certificates_lock[idx]);
  if (rid < replica->min_exec_rid[idx] || replica->last_replies[idx].find(rid)
      != replica->last_replies[idx].end())
  {
#ifdef MSG_DEBUG
    fprintf(stderr, "[FWD thread] req (%d, %qd) is an old request\n", cid, rid);
#endif
    delete req;
    pthread_mutex_unlock(&replica->certificates_lock[idx]);
    return ret;
  }

  std::unordered_map<Request_id, forwarding_cert>::const_iterator got = replica->certificates[idx].find(rid);
  if (got == replica->certificates[idx].end()) {   // new request
#ifdef MSG_DEBUG
    fprintf(stderr, "[FWD thread][add_request_to_certificate(%d, %qd, %d)] quorum = %d\n", cid, rid, node_id, 0);
#endif

    if (node_id != replica->id())
    {
      // we need to verify the request
      // verify only the signature
      if (!req->verify_request())
      {
        fprintf(stderr, "[FWD thread] req (%d, %qd) is not valid\n", cid, rid);
        delete req;
        return ret;
      }
    }

    forwarding_cert new_cert;
    new_cert.req_id = rid;
    new_cert.d = req->digest();
    new_cert.fwd_bitmap = new Bitmap(replica->num_replicas, false);
    new_cert.fwd_bitmap->set(node_id);
    new_cert.fwd_bitmap->set(replica->id()); // if node_id != replica->id(), I set the bit for myself

    if (node_id == replica->id()) {
      new_cert.r = req;
    } else {
      // need to clone the request as it is inlined in the Propagate message
      // which will be deleted at the end of the call of handle(Propagate*)
      new_cert.r = req->clone();
      delete req;
    }

    /*
     * if not enough place for the new request
     *    send propagate
     *
     */
    if (new_cert.r->size() > 1000) {
      zepropagate->add_request(new_cert.r);
      zepropagate->trim();
      zepropagate->authenticate();
      
#ifdef MSG_DEBUG
      Request *req = 0;
      Propagate::Requests_iter iterator(zepropagate);

      fprintf(stderr, "Sending propagate with the following requests:\n");
      while (iterator.get(&req))
      {
        Request_id rid = req->request_id();
        int cid = req->client_id();
        fprintf(stderr, "\t(%d, %qd)\n", cid, rid);
      }
#endif
      
      send_propagate(zepropagate);
      last_propagate_send = currentTime();
      zepropagate->clear();
    } else {
      if (!zepropagate->can_add_req(new_cert.r->size())) {
        zepropagate->trim();
        zepropagate->authenticate();

#ifdef MSG_DEBUG
        Request *req = 0;
        Propagate::Requests_iter iterator(zepropagate);

        fprintf(stderr, "Sending propagate with the following requests:\n");
        while (iterator.get(&req))
        {
          Request_id rid = req->request_id();
          int cid = req->client_id();
          fprintf(stderr, "\t(%d, %qd)\n", cid, rid);
        }
#endif

        send_propagate(zepropagate);
        last_propagate_send = currentTime();
        zepropagate->clear();
      }
      zepropagate->add_request(new_cert.r);
    }

    replica->certificates[idx][rid] = new_cert;
    ret = true;

  } else {
#ifdef MSG_DEBUG
    fprintf(stderr, "[FWD thread][add_request_to_certificate(%d, %qd, %d)] quorum = %d\n", cid, rid, node_id, got->second.fwd_bitmap->total_set());
#endif

    // add to existing quorum and send request to ordering if I can
    // I already have received a valid request.
    // I only need to compare the digests, unless the request comes from myself
    if (node_id == replica->id() || got->second.d == req->digest())
    {
      got->second.fwd_bitmap->set(node_id);
      if (got->second.fwd_bitmap->total_set() == 2 * replica->f() + 1)
      {
        Request *r = got->second.r;
        r->rep().replier = -1;

        bool ret = replica->request_buffer->cb_write_msg(r);
        if (ret)
        {
#ifdef MSG_DEBUG
          fprintf(stderr, "[FWD thread] Sending request (%d, %qd) to the verifier (@%p)\n", r->client_id(), r->request_id(), r);
#endif

#ifdef DEBUG_PERIODICALLY_PRINT_THR
          __sync_fetch_and_add (&replica->nb_sent_requests_to_verifier, 1);
#endif

          got->second.fwd_bitmap->setAll();
        }
        else
        {
#ifdef MSG_DEBUG
          //fprintf(stderr, "[FWD thread] WRITE FAILED!!!\n");
#endif
        }
      }
    }

    delete req;
  }

  pthread_mutex_unlock(&replica->certificates_lock[idx]);

  return ret;
}

void Forwarding_thread::exec_retrans_timer(void) {
  // call retransmit if the elapsed time since the last call is greater than the timeout
  if (currentTime() >= retrans_deadline) {
    
#ifdef MSG_DEBUG
    fprintf(stderr, "START RETRANS %qu\n", c_retrans);
    Time start = currentTime();
#endif

    retransmit();

#ifdef MSG_DEBUG
    long long elapsed = diffTime(currentTime(), start);
    fprintf(stderr, "STOP RETRANS %qu: %.2f ms\n", c_retrans++, elapsed / 1000.0);
#endif

    retrans_deadline = currentTime() + retrans_period;
  }
}

// send the Propagate if enough time has been elapsed since the last send
void Forwarding_thread::send_propagate_if_needed(void)
{
  if (!zepropagate->is_empty() && diffTime(currentTime(), last_propagate_send)
      / 1000.0 > PERIODIC_PROPAGATE_SEND)
  {
    zepropagate->trim();
    zepropagate->authenticate();

#ifdef MSG_DEBUG
    Request *req = 0;
    Propagate::Requests_iter iterator(zepropagate);

    fprintf(stderr, "Sending propagate with the following requests:\n");
    while (iterator.get(&req))
    {
      Request_id rid = req->request_id();
      int cid = req->client_id();
      fprintf(stderr, "\t(%d, %qd)\n", cid, rid);
    }
#endif

    send_propagate(zepropagate);
    zepropagate->clear();
    last_propagate_send = currentTime();
  }
}

void Forwarding_thread::retransmit(void) {
  Propagate pr;

  for (int idx=0; idx<replica->num_clients(); idx++) {
    pthread_mutex_lock(&replica->certificates_lock[idx]);
    for (auto it = replica->certificates[idx].begin(); it
        != replica->certificates[idx].end(); ++it)
    {
      forwarding_cert *cert = &it->second;
#ifdef MSG_DEBUG
      fprintf(stderr, "Retransmitting Propagate for req (%d, %qd): quorum is %d\n", idx+replica->num_replicas, cert->req_id, cert->fwd_bitmap->total_set());
#endif

      if (cert->r->size() > 1000) {
        pr.add_request(cert->r);
        pr.trim();
        pr.authenticate();
        send_propagate(&pr);
        pr.clear();
      } else {
        if (!pr.can_add_req(cert->r->size())) {
          pr.trim();
          pr.authenticate();
          send_propagate(&pr);
          pr.clear();
        }
        pr.add_request(cert->r);
      }
    }
    pthread_mutex_unlock(&replica->certificates_lock[idx]);
  }

  if (!pr.is_empty()) {
    pr.trim();
    pr.authenticate();
    send_propagate(&pr);
  }
}
