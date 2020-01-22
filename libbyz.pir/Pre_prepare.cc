#include <map>

#include "th_assert.h"
#include "Message_tags.h"
#include "Prepare.h"
#include "Pre_prepare.h"
#include "Request.h"
#include "Req_queue.h"
#include "Principal.h"
#include "MD5.h"
#if defined(PIR_ONLY)
#include "PIR.h"
#include "Req_list.h"
#include "z2z_async_dlist.h"
#elif defined(VERIFIER_ONLY)
#include "Verifier.h"
#endif

Pre_prepare::Pre_prepare(View v, Seqno s, Req_queue &reqs) :
  Message(Pre_prepare_tag, Max_message_size)
{
  rep().view = v;
  rep().seqno = s;

  //START_CC(pp_digest_cycles);
  //INCR_OP(pp_digest);

  // Fill in the request portion with as many requests as possible
  // and compute digest.
  Digest big_req_ds[big_req_max];
  int n_big_reqs = 0;
  char *next_req = requests();
#ifndef USE_PKEY
  char *max_req = next_req + msize() - replica->max_nd_bytes()
      - node->auth_size();
#else
  char *max_req = next_req+msize()-replica->max_nd_bytes()-node->sig_size();
#endif
  MD5_CTX context;
  MD5Init(&context);
  int count = 0;
  for (Request *req = reqs.first(); req != 0 && count < batch_size_limit; req
      = reqs.first(), count++)
  {
    // corruptClientMac() returns true if teh primary is corrupting
    // the client MAC
    if (corruptClientMAC())
      req->corrupt(corruptClientMAC());
    if (req->size() <= Request::big_req_thresh)
    {
#ifdef MSG_DEBUG
      fprintf(stderr, "\tAdding request %qd from %i to the Pre_prepare %qd\n",
          req->request_id(), req->client_id(), s);
#endif
      // Small requests are inlined in the pre-prepare message.
      if (next_req + req->size() <= max_req)
      {
        memcpy(next_req, req->contents(), req->size());
        MD5Update(&context, (char*) &(req->digest()), sizeof(Digest));
        next_req += req->size();
        delete reqs.remove();
      }
      else
      {
        break;
      }
    }
    else
    {
      // Big requests are sent offline and their digests are sent
      // with pre-prepare message.
      if (n_big_reqs < big_req_max && next_req + sizeof(Digest) <= max_req)
      {
        big_req_ds[n_big_reqs++] = req->digest();

        // Add request to replica's big reqs table.
        replica->big_reqs()->add_pre_prepare(reqs.remove(), s, v);
        max_req -= sizeof(Digest);
      }
      else
      {
        break;
      }
    }
  }
  rep().rset_size = next_req - requests();
  th_assert(rep().rset_size >= 0, "Request too big");

  // Put big requests after regular ones.
  for (int i = 0; i < n_big_reqs; i++)
    *(big_reqs() + i) = big_req_ds[i];
  rep().n_big_reqs = n_big_reqs;

  if (rep().rset_size > 0 || n_big_reqs > 0)
  {
    // Fill in the non-deterministic choices portion.
    int non_det_size = replica->max_nd_bytes();
    replica->compute_non_det(s, non_det_choices(), &non_det_size);
    rep().non_det_size = non_det_size;
  }
  else
  {
    // Null request
    rep().non_det_size = 0;
  }

  // Finalize digest of requests and non-det-choices.
  MD5Update(&context, (char*) big_reqs(), n_big_reqs * sizeof(Digest)
      + rep().non_det_size);
  MD5Final(rep().digest.udigest(), &context);

  //STOP_CC(pp_digest_cycles);

  // Compute authenticator and update size.
  int old_size = sizeof(Pre_prepare_rep) + rep().rset_size + rep().n_big_reqs
      * sizeof(Digest) + rep().non_det_size;

#ifndef USE_PKEY
  set_size(old_size + node->auth_size());
  node->gen_auth_out(contents(), sizeof(Pre_prepare_rep), contents() + old_size);
#else
  set_size(old_size+node->sig_size());
  node->gen_signature(contents(), sizeof(Pre_prepare_rep), contents()+old_size);
#endif

  trim();
}

#ifdef FAIRNESS_ATTACK
static const double delay = 0.0; // delay of the request in ms
static Time startTime = 0;
#endif

Pre_prepare::Pre_prepare(View v, Seqno s, Req_list *rlist) :
  Message(Pre_prepare_tag, Max_message_size)
{
  rep().view = v;
  rep().seqno = s;

  //START_CC(pp_digest_cycles);
  //INCR_OP(pp_digest);

  // Fill in the request portion with as many requests as possible
  // and compute digest.
  Digest big_req_ds[big_req_max];
  int n_big_reqs = 0;
  char *next_req = requests();
#ifndef USE_PKEY
  char *max_req = next_req + msize() - replica->max_nd_bytes()
      - node->auth_size();
#else
  char *max_req = next_req+msize()-replica->max_nd_bytes()-node->sig_size();
#endif
  MD5_CTX context;
  MD5Init(&context);
  int count = 0;

#ifdef FAIRNESS_ATTACK
  double waited_time = diffTime(currentTime(), startTime) / 1000.0;
#endif

  for (int i=0; i<replica->num_clients(); i++) {
    if (rlist->requests[i].size() == 0) {
      continue;
    }

#ifdef FAIRNESS_ATTACK
    if (replica->protocol_instance_id == FAULTY_PROTOCOL_INSTANCE && replica->id() == 0
            && i == 0 && waited_time < delay) {
      continue; // I'm byzantine and not fair
    }

    if (replica->protocol_instance_id == FAULTY_PROTOCOL_INSTANCE && replica->id() == 0 && i == 0) {
      startTime = currentTime();
    }
#endif

    // get a request
    std::map<Request_id, Request*>::iterator it = rlist->requests[i].begin();
    Request *req = it->second;

    if (corruptClientMAC())
      req->corrupt(corruptClientMAC());

    if (next_req + req->size() <= max_req)
    {
#ifdef MSG_DEBUG
      fprintf(stderr, "Adding request %qd from %d to the Pre_prepare %qd\n",
          req->request_id(), req->client_id(), s);
#endif

      memcpy(next_req, req->contents(), req->size());
      MD5Update(&context, (char*) &(req->digest()), sizeof(Digest));
      next_req += req->size();
      rlist->reqs_for_primary_size--;
      delete req;
      rlist->requests[i].erase(it);
    }
    else
    {
      break;
    }

    if (++count >= batch_size_limit)
    {
      break;
    }
  }

  rep().rset_size = next_req - requests();
  th_assert(rep().rset_size >= 0, "Request too big");

  // Put big requests after regular ones.
  for (int i = 0; i < n_big_reqs; i++)
    *(big_reqs() + i) = big_req_ds[i];
  rep().n_big_reqs = n_big_reqs;

  if (rep().rset_size > 0 || n_big_reqs > 0)
  {
    // Fill in the non-deterministic choices portion.
    int non_det_size = replica->max_nd_bytes();
    replica->compute_non_det(s, non_det_choices(), &non_det_size);
    rep().non_det_size = non_det_size;
  }
  else
  {
    // Null request
    rep().non_det_size = 0;
  }

  // Finalize digest of requests and non-det-choices.
  MD5Update(&context, (char*) big_reqs(), n_big_reqs * sizeof(Digest)
      + rep().non_det_size);
  MD5Final(rep().digest.udigest(), &context);

  //STOP_CC(pp_digest_cycles);

  // Compute authenticator and update size.
  int old_size = sizeof(Pre_prepare_rep) + rep().rset_size + rep().n_big_reqs
      * sizeof(Digest) + rep().non_det_size;

#ifndef USE_PKEY
  set_size(old_size + node->auth_size());
  node->gen_auth_out(contents(), sizeof(Pre_prepare_rep), contents() + old_size);
#else
  set_size(old_size+node->sig_size());
  node->gen_signature(contents(), sizeof(Pre_prepare_rep), contents()+old_size);
#endif

  trim();
}


Pre_prepare* Pre_prepare::clone(View v) const
{
  Pre_prepare *ret = (Pre_prepare*) new Message(max_size);
  memcpy(ret->msg, msg, msg->size);
  ret->rep().view = v;
  return ret;
}

void Pre_prepare::re_authenticate(Principal *p)
{
#ifndef USE_PKEY 
  node->gen_auth_out(contents(), sizeof(Pre_prepare_rep), non_det_choices()
      + rep().non_det_size);
#endif 
}

int Pre_prepare::id() const
{
  return replica->primary(view());
}

bool Pre_prepare::check_digest()
{
  // Check sizes
#ifndef USE_PKEY
  int min_size = sizeof(Pre_prepare_rep) + rep().rset_size + rep().n_big_reqs
      * sizeof(Digest) + rep().non_det_size + node->auth_size(replica->primary(
      view()));
#else
  int min_size = sizeof(Pre_prepare_rep)+rep().rset_size+rep().n_big_reqs*sizeof(Digest)
  +rep().non_det_size+node->sig_size(replica->primary(view()));
#endif
  if (size() >= min_size)
  {
    //START_CC(pp_digest_cycles);
    //INCR_OP(pp_digest);

    // Check digest.
    MD5_CTX context;
    MD5Init(&context);
    Digest d;
    Request req;
    char *max_req = requests() + rep().rset_size;
    for (char *next = requests(); next < max_req; next += req.size())
    {
      if (Request::convert(next, max_req - next, req))
      {
        MD5Update(&context, (char*) &(req.digest()), sizeof(Digest));
      }
      else
      {
        //STOP_CC(pp_digest_cycles);
        return false;
      }
    }

    // Finalize digest of requests and non-det-choices.
    MD5Update(&context, (char*) big_reqs(), rep().n_big_reqs * sizeof(Digest)
        + rep().non_det_size);
    MD5Final(d.udigest(), &context);

    //STOP_CC(pp_digest_cycles);
    return d == rep().digest;
  }
  return false;
}

bool Pre_prepare::verify(int mode)
{
  int sender = replica->primary(view());

  // Check sizes and digest.
  int sz = rep().rset_size + rep().n_big_reqs * sizeof(Digest)
      + rep().non_det_size;
#ifndef USE_PKEY
  int min_size = sizeof(Pre_prepare_rep) + sz + node->auth_size(
      replica->primary(view()));
#else
  int min_size = sizeof(Pre_prepare_rep)+sz+node->sig_size(replica->primary(view()));
#endif
  if (size() >= min_size)
  {
    //INCR_OP(pp_digest);

    // Check digest.
    Digest d;
    MD5_CTX context;
    MD5Init(&context);
    Request req;
    char* max_req = requests() + rep().rset_size;
    for (char *next = requests(); next < max_req; next += req.size())
    {
      // no need to verify the requests
      if (Request::convert(next, max_req - next, req) /*&& (mode == NRC
          || replica->has_req(req.client_id(), req.digest()) || req.size() == sizeof(Request_rep) || req.verify())*/)
      {
        //START_CC(pp_digest_cycles);

        MD5Update(&context, (char*) &(req.digest()), sizeof(Digest));

        //STOP_CC(pp_digest_cycles);
      }
      else
      {
        return false;
      }

      // TODO: If we batch requests from different clients. We need to
      // change this a bit. Otherwise, a good client could be denied
      // service just because its request was batched with the request
      // of another client.  A way to do this would be to include a
      // bitmap with a bit set for each request that verified.
    }

    //START_CC(pp_digest_cycles);

    // Finalize digest of requests and non-det-choices.
    MD5Update(&context, (char*) big_reqs(), rep().n_big_reqs * sizeof(Digest)
        + rep().non_det_size);
    MD5Final(d.udigest(), &context);

    //STOP_CC(pp_digest_cycles);

#ifndef USE_PKEY
    if (d == rep().digest)
    {
      return mode == NAC || node->verify_auth_in(sender, contents(),
          sizeof(Pre_prepare_rep), requests() + sz);
    }
#else
    if (d == rep().digest)
    {
      Principal* ps = node->i_to_p(sender);
      return mode == NAC
      || ps->verify_signature(contents(), sizeof(Pre_prepare_rep), requests()+sz);
    }

#endif
  }
  return false;
}

Pre_prepare::Requests_iter::Requests_iter(Pre_prepare *m)
{
  msg = m;
  next_req = m->requests();
  big_req = 0;
}

bool Pre_prepare::Requests_iter::get(Request &req)
{
  if (next_req < msg->requests() + msg->rep().rset_size)
  {
    req = Request((Request_rep*) next_req);
    next_req += req.size();
    return true;
  }

  if (big_req < msg->num_big_reqs())
  {
    Request* r = replica->big_reqs()->lookup(msg->big_req_digest(big_req));
    th_assert(r != 0, "Missing big req");
    req = Request((Request_rep*) r->contents());
    big_req++;
    return true;
  }

  return false;
}

bool Pre_prepare::convert(Message *m1, Pre_prepare *&m2)
{
  if (!m1->has_tag(Pre_prepare_tag, sizeof(Pre_prepare_rep)))
    return false;

  m2 = (Pre_prepare*) m1;
  m2->trim();
  return true;
}

