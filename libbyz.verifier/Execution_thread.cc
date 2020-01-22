/*
 * execution_thread.cpp
 *
 *  Created on: 3 mai 2011
 *      Author: benmokhtar
 */

#include "Execution_thread.h"
#include <stdio.h>
#include <unistd.h>

#include "Message.h"
#include "Propagate.h"
#include "Verifier.h"
#include "attacks.h"


void *Execution_thread_startup(void *_tgtObject)
{
  printf("Execution thread startup....\n");

  Execution_thread *tgtObject = (Execution_thread *) _tgtObject;
  void *threadResult = tgtObject->run();
  delete tgtObject;
  return threadResult;
}

// Look for the request (cid, rid) in the certificates
// If present, then return the request and delete the corresponding certificate
// Otherwise, return NULL
Request* Execution_thread::get_request_from_certificate(int cid, Request_id rid) {
  Request *r = NULL;

  std::unordered_map<Request_id, forwarding_cert>::iterator it = replica->certificates[cid].find(rid);
  if (it != replica->certificates[cid].end())
  {
    r = it->second.r;
#ifdef MSG_DEBUG
    fprintf(stderr, "[%s] Deleting certificate for (%d, %qd)\n", __PRETTY_FUNCTION__, r->client_id(), r->request_id());
#endif
    delete it->second.fwd_bitmap;
    replica->certificates[cid].erase(it);
  }

  return r;
}

// delete all certificates (and the associated request) for client cid whose
// id is less or equal to rid
void Execution_thread::delete_old_certificates(int cid, Request_id rid) {
  for (std::unordered_map<Request_id, forwarding_cert>::iterator it = replica->certificates[cid].begin(); it != replica->certificates[cid].end(); ) {
    if (it->first <= rid) {
#ifdef MSG_DEBUG
      Request *r = it->second.r;
      fprintf(stderr, "[%s] Deleting certificate for (%d, %qd)\n", __PRETTY_FUNCTION__, r->client_id(), r->request_id());
#endif
      delete it->second.r;
      delete it->second.fwd_bitmap;
      replica->certificates[cid].erase(it++);
    } else {
        it++;
    }
  }
}

void Execution_thread::handle(Wrapped_request *m) {
  Request req;
  long long outbSize = 0;
  Wrapped_request::Requests_iter iterator(m);

#ifdef DEBUG_PERIODICALLY_PRINT_THR
  __sync_fetch_and_add (&replica->nb_recv_requests_from_verifier, m->client_id());
#endif

  replica->last_tentative_execute = replica->last_executed + 1;

  while (iterator.get(req))
  {
    // Obtain "in" and "out" buffers to call exec_command
    Byz_req inb;
    Byz_rep outb;
    Byz_buffer non_det;
    int cid = req.client_id();
    int idx = cid - replica->num_replicas;
    Request_id rid = req.request_id();

#ifdef PERIODICALLY_MEASURE_THROUGHPUT
    replica->nb_requests_4_periodic_thr_measure++;
#endif

    if (replica->first_request_time == 0)
    {
      replica->first_request_time = currentTime();
    }

    pthread_mutex_lock(&replica->certificates_lock[idx]);

    Request *r = get_request_from_certificate(idx, rid);
    if (!r)
    {
#ifdef MSG_DEBUG
      fprintf(stderr, "Execution thread: I do not have a certificate for request (%d, %qd)\n", cid, rid);
#endif
      pthread_mutex_unlock(&replica->certificates_lock[idx]);
      continue;
    }
#ifdef MSG_DEBUG
    else
    {
      //fprintf(stderr, "Execution thread: Request (%d, %qd) @%p\n", cid, rid, r);
    }
#endif

    inb.contents = r->command(inb.size);
    outb.contents = replica->replies.new_reply(cid, outb.size);

    if (replica->is_replica(cid))
    {
      // Handle recovery requests, i.e., requests from replicas,
      // differently.  to-do: make more general to allow other types
      // of requests from replicas.
      //    fprintf(stderr, "\n\n\nExecuting recovery request seqno= %qd rep id=%d\n", last_tentative_execute, cid);

      if (inb.size != sizeof(Seqno))
      {
        // Invalid recovery request.
        pthread_mutex_unlock(&replica->certificates_lock[idx]);
        continue;
      }

      // Change keys. to-do: could change key only for recovering replica.
      if (cid != replica->node_id)
        replica->send_new_key();

      // Store seqno of execution.
      replica->max_rec_n = replica->last_tentative_execute;

      // Reply includes sequence number where request was executed.
      outb.size = sizeof(replica->last_tentative_execute);
      memcpy(outb.contents, &replica->last_tentative_execute, outb.size);
    }
    else
    {
#ifdef MSG_DEBUG
      fprintf(stderr, "Executor: Executing request %qd from client %d\n", rid, cid);
#endif
      replica->nb_executed++;

      replica->exec_command(&inb, &outb, &non_det, cid, false,
          replica->exec_command_delay);

      if (outb.size % ALIGNMENT_BYTES)
        for (int i = 0; i < ALIGNMENT_BYTES - (outb.size
            % ALIGNMENT_BYTES); i++)
          outb.contents[outb.size + i] = 0;
    }
    outbSize += outb.size;

    // Finish constructing the reply.
    replica->replies.end_reply(cid, rid, outb.size);

    // Send the reply. replica->replies to recovery requests are only sent
    // when the request is committed.
    if (outb.size != 0 && !replica->is_replica(cid))
    {
      if (outb.size < 50 || req.replier() == replica->node_id
          || req.replier() < 0)
      {
        // Send full reply.
#ifdef MSG_DEBUG
        fprintf(stderr, "Executor %i: Sending a reply to client %i for request %qu\n", replica->id(), cid, rid);
#endif
        replica->replies.send_reply(cid, replica->view(), replica->id(),
            false);
#ifdef DEBUG_PERIODICALLY_PRINT_THR
        __sync_fetch_and_add (&replica->nb_sent_replies, 1);
#endif

        // if rid < min_exec_rid[idx] then it is an old reply that will never be retransmitted,
        // so just send it to please the client
        if (rid >= replica->min_exec_rid[idx]) {
          Reply * rep = replica->replies.reply(cid)->copy(replica->replies.reply(cid)->id());
          replica->last_replies[idx][rep->request_id()] = rep;

          if (replica->last_replies[idx].size() > MISSING_RID_MAX_RANGE)
          {
            std::map<Request_id, Reply*>::iterator it =
                replica->last_replies[idx].begin();
            Request *old_req = get_request_from_certificate(idx, it->first);
            if (old_req) delete old_req;
            delete it->second;
            replica->last_replies[idx].erase(it++);
            replica->min_exec_rid[idx] = it->first;
          }
        }

        // delete certificates up to rid
        //delete_old_certificates(idx, rid);
      }
      else
      {
        // Send empty reply.
        Reply empty(replica->view(), rid, replica->node_id,
            replica->replies.digest(cid), replica->i_to_p(cid), false);
#ifdef MSG_DEBUG
        fprintf(stderr, "Executor %i: Sending an empty reply to client %i\n", replica->id(), cid);
#endif
        replica->send(&empty, cid);
      }
    }

    pthread_mutex_unlock(&replica->certificates_lock[idx]);

    delete r;
  }
}

void Execution_thread::handle(Request *req)
{
  if (req->is_read_only())
  {
    th_assert(false, "Read-only requests are not handled yet at the execution thread\n");
  }
  else
  {
    //retransmit
    Request_id rid = req->request_id();
    int idx = req->client_id() - replica->num_replicas;

    pthread_mutex_lock(&replica->certificates_lock[idx]);

    if (rid < replica->min_exec_rid[idx])
    {
      // retransmit reply replica->min_exec_rid[cid] and say that it is not necessary to wait for older replies (view=-1)
      //replica->last_replies[idx][replica->min_exec_rid[idx]]->set_view(-1);
      //replica->send(replica->last_replies[idx][replica->min_exec_rid[rid]], req->client_id());
      
        //std::map<Request_id, Reply*>::iterator it = replica->last_replies[idx].find(replica->min_exec_rid[idx]);
        std::map<Request_id, Reply*>::iterator it = replica->last_replies[idx].begin();
        if (it != replica->last_replies[idx].end()) {
          it->second->set_view(-1);
          replica->send(it->second, req->client_id());
#ifdef DEBUG_PERIODICALLY_PRINT_THR
        __sync_fetch_and_add (&replica->nb_sent_replies, 1);
#endif

#ifdef MSG_DEBUG
          fprintf(stderr, "Executor %i: Sending last req %qd to client %d and ask to forget older ones\n", replica->id(), it->first, req->client_id());
#endif
        }
    }
    else
    {
      // retransmit reply
      if (replica->last_replies[idx].find(rid) != replica->last_replies[idx].end())
      {
        replica->send(replica->last_replies[idx][rid], req->client_id());
#ifdef DEBUG_PERIODICALLY_PRINT_THR
        __sync_fetch_and_add (&replica->nb_sent_replies, 1);
#endif

#ifdef MSG_DEBUG
          fprintf(stderr, "Executor %i: Sending last req %qd to client %d\n", replica->id(), rid, req->client_id());
#endif
      }
    }

    pthread_mutex_unlock(&replica->certificates_lock[idx]);

    replica->nb_retransmissions++;
  }
}

void *Execution_thread::run(void)
{
  while (!replica)
  {
    fprintf(stderr, "replica not initialized yet...\n");
    sleep(1);
  }

  Message *m;

  while (1)
  {
    m = replica->verifier_to_executor_buffer->bcb_read_msg();

    if (m && m != replica->verifier_to_executor_buffer->bcb_magic())
    {
      if (m->tag() == Wrapped_request_tag)
      {
        handle((Wrapped_request*) m);
      }
      else if (m->tag() == Request_tag)
      {
        handle((Request*) m);
      }
      else
      {
        fprintf(stderr, "Execution thread: received a message with tag %d\n",
            m->tag());
      }

      delete m;
    }
    else
    {
#ifdef MSG_DEBUG
      fprintf(stderr, "Execution thread: Nothing to read\n");
#endif
    }
  }

  return NULL;
}

