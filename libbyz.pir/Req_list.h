/* class Req_list
 *
 * A Req_list contains a list of requests, for different clients.
 * Contrary to Req_queue, you can find different requests for the same client.
 */

#ifndef _REQ_LIST_
#define _REQ_LIST_

#include <map>
#include "parameters.h"
#include "Request.h"


class Req_list
{
public:
  // create a Req_list for nc clients
  // nr is the number of replicas. it is used because client ids start at nr, not 0
  Req_list(int nc, int nr);

  // delete a Req_list
  ~Req_list(void);

  // add a request to the Req_list
  void add_request(Request *r);

  // add a request to the Req_list, for the primary
  void add_request_for_primary(Request *r);

  // look for the request r (cid, rid and digest) in the Req_list.
  // If it is present, then return true, false otherwise
  bool look_for_request(Request *r);

  // look for the request with (cid, rid) in the Req_list.
  // If it is present, then delete it
  void delete_request(int cid, Request_id rid);

  // delete all requests (cid, r) in the Req_list
  // such that r <= rid
  void delete_request_up_to_rid(int cid, Request_id rid);

  // print the content of the min_ordered_rid array
  void print_min_ordered_array(void);

  // print the content of the Req_list
  void print_content(void);

  // return true if reqs_for_primary is empty, false otherwise
  bool reqs_for_primary_empty(void);

  // empty the list of pending requests
  void empty_pending_reqs_list(void);

  // list of requests at the primary, to create pre-prepare
  long reqs_for_primary_size;
  Request_id *min_ordered_rid;
  std::map<Request_id, char> *ordered_rid;
  std::map<Request_id, Request*> *requests;

private:
  int num_clients;
  int num_replicas;
};

// add a request to the Req_list
inline void Req_list::add_request(Request *r)
{
  int idx = r->client_id() - num_replicas;

  if (r->request_id() > min_ordered_rid[idx])
  {
    requests[idx][r->request_id()] = r;
  }
  else
  {
    delete r;
  }
}

// add a request to the Req_list, for the primary
// Req_list must be kept ordered
inline void Req_list::add_request_for_primary(Request *r)
{
  int idx = r->client_id() - num_replicas;

  if (r->request_id() > min_ordered_rid[idx])
  {
    requests[idx][r->request_id()] = r;
    reqs_for_primary_size++;
    if (min_ordered_rid[idx] + 1 == r->request_id())
    {
      min_ordered_rid[idx]++;
    }
  }
  else
  {
#ifdef MSG_DEBUG
    fprintf(stderr, "Primary cannot add request (%d, %qd): %qd <= %qd\n",
            r->client_id(), r->request_id(), r->request_id(), min_ordered_rid[idx]);
#endif
    delete r;
  }
}

// return true if reqs_for_primary is empty, false otherwise
inline bool Req_list::reqs_for_primary_empty(void)
{
  return (reqs_for_primary_size == 0);
}
#endif
