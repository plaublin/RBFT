/*
 * Propagate.cc
 *
 *  Created on: Sep 18, 2011
 *      Author: benmokhtar
 */

#include <stdio.h>

#include "Propagate.h"
#include "Message_tags.h"
#include "Node.h"

Propagate::Propagate(void):Message(Propagate_tag, Max_message_size)
{
  rep().id = node->id();
  rep().rset_size = 0;
}

Propagate::~Propagate()
{
}

void Propagate::authenticate(){
  char *dest = contents() + sizeof(Propagate_rep) + rep().rset_size;
  node->gen_auth_out(contents(), sizeof(Propagate_rep), dest);
}

bool Propagate::verify() {
  // Propagate must be sent by replicas.
  if (!node->is_replica(id()) || id() == node->id()) return false;

  // Check Mac size.
  if (size()-(int)sizeof(Propagate_rep)-rep().rset_size < node->auth_size(id()))
    return false;

  char *dest = contents() + sizeof(Propagate_rep) + rep().rset_size;
  return node->verify_auth_in(id(), contents(), sizeof(Propagate_rep), dest);
}

void Propagate::print(void) {
    fprintf(stderr, "====================\nPropagate of id %i and size %i. Content is:\n", rep().id, size());

    for (char* p = contents(); p < (contents() + size()); p++) {
        fprintf(stderr, " %i", (int)*p);
    }
    fprintf(stderr, "\n====================\n");
}

Propagate::Requests_iter::Requests_iter(Propagate *m)
{
  msg = m;
  next_req = m->requests();
}

bool Propagate::Requests_iter::get(Request **req)
{
  if (next_req < msg->requests() + msg->rep().rset_size)
  {
    *req = new Request((Request_rep*) next_req);
    next_req += (*req)->size();
    return true;
  }

  return false;
}
