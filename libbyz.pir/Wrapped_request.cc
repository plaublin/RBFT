#include "th_assert.h"
#include "Message_tags.h"
#include "Prepare.h"
#include "Pre_prepare.h"
#include "Request.h"
#include "Req_queue.h"
#include "Principal.h"
#include "MD5.h"
#include "Wrapped_request.h"
#include "parameters.h"
#if defined(PIR_ONLY)
#include "PIR.h"
#elif defined(VERIFIER_ONLY)
#include "Verifier.h"
#endif

Wrapped_request::Wrapped_request(View v, Seqno s, Req_queue &reqs,
    bool faultyClient) :
  Message(Wrapped_request_tag, Max_message_size)
{
  rep().view = node->id(); // view is used to store the client id!!!
  rep().seqno = s;

  //START_CC(pp_digest_cycles);
  //INCR_OP(pp_digest);

  // Fill in the request portion with as many requests as possible
  // and compute digest.
  Digest big_req_ds[big_req_max];
  int n_big_reqs = 0;
  char *next_req = requests();
#ifndef USE_PKEY
  char *max_req = next_req + msize()
      - /*replica->max_nd_bytes()-*/node->auth_size();
#else 
  char *max_req = next_req+msize()-/*node->max_nd_bytes()-*/node->sig_size();
#endif
  MD5_CTX context;
  MD5Init(&context);

  int count = 0;
  for (Request *req = reqs.first(); req != 0 && count < batch_size_limit; req
      = reqs.first(), count++)
  {
    //PL: we add all the requests of reqs, not only the first one.
    //Request *req = reqs.first();

    //fprintf(stderr, "!inside the iterator!");
    // corruptClientMac() returns true if teh primary is corrupting
    // the client MAC
    if (req->size() <= Request::big_req_thresh)
    {
#ifdef MSG_DEBUG
      fprintf(
          stderr,
          "\tAdding request %qd from %i to the wrapped_request %qd  (for the verifier)\n",
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
        //break;
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
        //break;
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
    //int non_det_size = replica->max_nd_bytes();
    int non_det_size = 0;
    //replica->compute_non_det(s, non_det_choices(), &non_det_size);
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
  int old_size = sizeof(Wrapped_request_rep) + rep().rset_size
      + rep().n_big_reqs * sizeof(Digest) + rep().non_det_size;

#ifndef USE_PKEY
  set_size(old_size + node->auth_size());
  if (node->is_replica(node->id()))
    node->gen_auth_out(contents(), sizeof(Wrapped_request_rep), contents()
        + old_size); // replica -> replica
  else
    node->gen_auth_in(contents(), sizeof(Wrapped_request_rep), contents()
        + old_size, faultyClient); // client -> replica
#else 
  set_size(old_size+node->sig_size());
  node->gen_signature(contents(), sizeof(Wrapped_request_rep), contents()+old_size);
#endif

  trim();
}

// Effects: Creates a Wrapped_request that will contain all the requests present
// in the Pre_prepare pp. It will be with view number "v" and sequence number "s".
Wrapped_request::Wrapped_request(View v, Seqno s, Pre_prepare *pp) :
    Message(Wrapped_request_tag, Max_message_size) {
  rep().extra = replica->protocol_instance_id; // extra stores the protocol instance id of this replica
  rep().view = 0; // view stores the number of requests in this wrapped request
  rep().seqno = s;

#ifdef MSG_DEBUG
  fprintf(stderr, "NEW WRAPPED_REQUEST: %qd,\n", s);
#endif

  // Fill in the request portion with as many requests as possible
  // and compute digest.
  Digest big_req_ds[big_req_max];
  int n_big_reqs = 0;
  char *next_req = requests();
#ifndef USE_PKEY
  char *max_req = next_req + msize()
      - /*replica->max_nd_bytes()-*/node->auth_size();
#else
  char *max_req = next_req+msize()-/*node->max_nd_bytes()-*/node->sig_size();
#endif

  Pre_prepare::Requests_iter iter(pp);
  Request req;

  while (iter.get(req))
  {
    // Small requests are inlined in the pre-prepare message.
    if (next_req + req.size() <= max_req)
    {
#ifdef MSG_DEBUG
  fprintf(stderr, "Adding ordered request %qd from client %d\n", req.request_id(), req.client_id());
#endif

      memcpy(next_req, req.contents(), req.size());
      next_req += req.size();
      rep().view++;
    }
    else
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
    //int non_det_size = replica->max_nd_bytes();
    int non_det_size = 0;
    //replica->compute_non_det(s, non_det_choices(), &non_det_size);
    rep().non_det_size = non_det_size;
  }
  else
  {
    // Null request
    rep().non_det_size = 0;
  }

  //STOP_CC(pp_digest_cycles);

  // Compute authenticator and update size.
  int old_size = sizeof(Wrapped_request_rep) + rep().rset_size
      + rep().n_big_reqs * sizeof(Digest) + rep().non_det_size;

#ifndef USE_PKEY
  set_size(old_size + node->auth_size());
#else
  set_size(old_size+node->sig_size());
#endif

  trim();
}

void Wrapped_request::re_authenticate(Principal *p)
{
#ifndef USE_PKEY 
  node->gen_auth_out(contents(), sizeof(Wrapped_request_rep), non_det_choices()
      + rep().non_det_size);
#endif 
}

bool Wrapped_request::verify_MAC(int mode)
{
  int sender = (int) rep().view; //view is used to store the client id

  /*
   printf(
   "[replica %i][primary %i] Verifying mac of request %qd from client %i\n",
   replica->id(), replica->primary(), this->seqno(), sender);
   */

  // Check sizes and digest.
  int sz = rep().rset_size + rep().n_big_reqs * sizeof(Digest)
      + rep().non_det_size;
#ifndef USE_PKEY
  int min_size = sizeof(Wrapped_request_rep) + sz + node->auth_size(sender);
#else
  int min_size = sizeof(Wrapped_request_rep)+sz+node->sig_size(sender);
#endif

  if (size() < min_size)
  {
    /*
     fprintf(stderr, "[replica %i][primary %i] request %qd from client %i, %i < %i\n",
     replica->id(), replica->primary(), this->seqno(), sender, size(),
     min_size);
     */
    return false;
  }

  // Check digest.
  Digest d;
  MD5_CTX context;
  MD5Init(&context);
  Request req;
  //START_CC(pp_digest_cycles);

  char* max_req = requests() + rep().rset_size;
  char* next = requests(); //and first, and last

  if (!Request::convert(next, max_req - next, req))
  {
    /*
     }
     printf(
     "[replica %i][primary %i] request %qd from client %i, convert has failed\n",
     replica->id(), replica->primary(), this->seqno(), sender);
     */
    return false; //unable to extract requests
  }

  //if i would not append the request, throw it away immediately
  if (!replica->rqueue.wouldAppend(&req))
  {
    /*
     }
     // THIS IS HERE THAT THE PROBLEM ARISES
     printf(
     "[replica %i][primary %i] request %qd from client %i, wouldAppend false\n",
     replica->id(), replica->primary(), this->seqno(), sender);
     */
    return false; //useless
  }

  // Finalize digest of requests and non-det-choices.
  MD5Update(&context, (char*) big_reqs(), rep().n_big_reqs * sizeof(Digest)
      + rep().non_det_size);
  MD5Final(d.udigest(), &context);

  //STOP_CC(pp_digest_cycles);

#ifndef USE_PKEY

  //START_CC(pp_digest_cycles);
  MD5Update(&context, (char*) &(req.digest()), sizeof(Digest));
  //STOP_CC(pp_digest_cycles);


  if (d == rep().digest)
  {

    bool auth_ok;

    if (node->is_replica(sender))
      auth_ok = node->verify_auth_in(sender, contents(),
          sizeof(Wrapped_request_rep), requests() + sz); // replica -> replica
    else
      auth_ok = node->verify_auth_out(sender, contents(),
          sizeof(Wrapped_request_rep), requests() + sz); // client -> replica

    if (!(mode == NAC || auth_ok))
    {
      /*
       printf(
       "[replica %i][primary %i] request %qd from client %i, digest ok but not authenticated\n",
       replica->id(), replica->primary(), this->seqno(), sender);
       */
      // digest ok, but not authenticated
      return false;
    }

  }
#else
  if (d == rep().digest)
  {
    Principal* ps = node->i_to_p(sender);
    if ( ! (mode == NAC
            || ps->verify_signature(contents(), sizeof(Wrapped_request_rep), requests()+sz)))
    {
      //digest ok, but not authenticated
      return false;
      {
      }
      else
      {
        //different digest
        return false;
      }

#endif

  // authenticating the signature in the verifier thread now!
  // the verifier thread will call this function to verity
  // digest and mac. If this function returns false, the request
  // will be dropped without other consequences.
  // If this function returns true, the verifier thread knows
  // that digest and MAC are ok. After that, the verifier thread
  // will verifi the request's signature. If the signature verifies,
  // then the request is ordered.
  // if the sugnature does not verify, we assume that the client is
  // trying to do something weird, and blacklist the client.
  return true;

  /*

   request verification code, moved inside the verifier thread.
   // i made it through there, so both digest and authentication are ok.
   // now verify the request inside (with signatures)

   //INCR_OP(pp_digest);
   if( mode == NRC || req.verify() ) {
   // digest, authentication, and requests are ok
   return true;

   } else {
   //digest and authentication ok, request KO!
   return false;
   }

   return false;
   */

}

bool Wrapped_request::verify_request(int mode)
{
  char* max_req = requests() + rep().rset_size;
  char* next = requests(); //and first, and last

  Request req;

  if (!Request::convert(next, max_req - next, req))
    return false; //unable to extract requests

  if (mode == NRC || req.verify())
  {
    return true; // ok, request verified
  }

  return false; //KO, request NOT verified

}

Wrapped_request::Requests_iter::Requests_iter(Wrapped_request *m)
{
  msg = m;
  next_req = m->requests();
  big_req = 0;
}

bool Wrapped_request::Requests_iter::get(Request &req)
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

bool Wrapped_request::convert(Message *m1, Wrapped_request *&m2)
{
  if (!m1->has_tag(Pre_prepare_tag, sizeof(Pre_prepare_rep)))
    return false;

  m2 = (Wrapped_request*) m1;
  m2->trim();
  return true;
}

