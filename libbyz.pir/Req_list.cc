/* class Req_list
 *
 * A Req_list contains a list of requests, for different clients.
 * Contrary to Req_queue, you can find different requests for the same client.
 */

#include <stdio.h>
#include "Req_list.h"

// create a Req_list for nc clients
// nr is the number of replicas. it is used because client ids start at nr, not 0
Req_list::Req_list(int nc, int nr)
{
  num_clients = nc;
  num_replicas = nr;
  min_ordered_rid = new Request_id[num_clients];
  ordered_rid = new std::map<Request_id, char>[num_clients];
  requests = new std::map<Request_id, Request*>[num_clients];
  for (int i = 0; i < num_clients; i++)
  {
    min_ordered_rid[i] = 0;
  }

  reqs_for_primary_size = 0;
}

// delete a Req_list
Req_list::~Req_list(void)
{
  for (int i = 0; i < num_clients; i++)
  {
    for (std::map<Request_id, Request*>::iterator it = requests[i].begin(); it
        != requests[i].end();)
    {
      delete it->second;
      requests[i].erase(it++);
    }
  }
  delete[] requests;
  delete[] ordered_rid;
  delete[] min_ordered_rid;
}

// look for the request r (cid, rid) in the Req_list.
// If it is present, then return true, false otherwise.
bool Req_list::look_for_request(Request *r)
{
  int idx = r->client_id() - num_replicas;
  std::map<Request_id, Request*>::iterator it = requests[idx].find(r->request_id());

  return (it != requests[idx].end());
}

// look for the request with (cid, rid) in the Req_list.
// If it is present, then delete it
void Req_list::delete_request(int cid, Request_id rid)
{
  int idx = cid - num_replicas;
  std::map<Request_id, Request*>::iterator it = requests[idx].find(rid);

  if (it != requests[idx].end())
  {
#ifdef MSG_DEBUG
    fprintf(stderr, "[%s] Deleting req (%d, %qd)\n", __PRETTY_FUNCTION__, it->second->client_id(), it->second->request_id());
#endif

    delete it->second;
    requests[idx].erase(it);

    ordered_rid[idx][rid] = 1;
    std::map<Request_id, char>::iterator it2 = ordered_rid[idx].begin();
    while (it2->first == min_ordered_rid[idx] + 1)
    {
      min_ordered_rid[idx]++;
      ordered_rid[idx].erase(it2);
      it2 = ordered_rid[idx].begin();
    }
  }
}

// delete all requests (cid, r) in the Req_list
// such that r <= rid
void Req_list::delete_request_up_to_rid(int cid, Request_id rid) {
  int idx = cid - num_replicas;

  for (std::map<Request_id, Request*>::iterator it = requests[idx].begin(); it
      != requests[idx].end();)
  {
    if (it->second->request_id() <= rid)
    {
#ifdef MSG_DEBUG
    fprintf(stderr, "[%s] Deleting req (%d, %qd)\n", __PRETTY_FUNCTION__, it->second->client_id(), it->second->request_id());
#endif

      delete it->second;
      requests[idx].erase(it++);

      std::map<Request_id, char>::iterator it2 = ordered_rid[idx].begin();
      while (it2->first == min_ordered_rid[idx] + 1)
      {
        min_ordered_rid[idx]++;
        ordered_rid[idx].erase(it2);
        it2 = ordered_rid[idx].begin();
      }
    }
    else
    {
      break;
    }
  }
}

// print the content of the min_ordered_rid array
void Req_list::print_min_ordered_array(void) {

  fprintf(stderr, "min_ordered_rid\ncid\trid\n");

  for (int i = 0; i < num_clients; i++)
  {
    if (min_ordered_rid[i] > 0)
    {
      fprintf(stderr, "%d\t%qd\n", i + num_replicas, min_ordered_rid[i]);
    }
  }
}

// print the content of the Req_list
void Req_list::print_content(void)
{
  fprintf(stderr, "Requests in rlist\ncid\trid\n");

  for (int i = 0; i < num_clients; i++)
  {
    for (std::map<Request_id, Request*>::iterator it = requests[i].begin(); it
        != requests[i].end(); it++)
    {
      Request *r = it->second;
      fprintf(stderr, "%d\t%qd\n", r->client_id(), r->request_id());
    }
  }

  fprintf(stderr, "\n==============================\n");
}

// empty the list of pending requests
void Req_list::empty_pending_reqs_list(void)
{
  for (int i = 0; i < num_clients; i++)
  {
    for (std::map<Request_id, Request*>::iterator it = requests[i].begin(); it
        != requests[i].end();)
    {
#ifdef MSG_DEBUG
      fprintf(stderr, "[%s] Deleting req (%d, %qd)\n", __PRETTY_FUNCTION__, it->second->client_id(), it->second->request_id());
#endif
      delete it->second;
      requests[i].erase(it++);
    }
  }
}
