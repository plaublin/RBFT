/*
 * Propagate.h
 *
 *  Created on: Sep 18, 2011
 *      Author: benmokhtar
 */

#ifndef PROPAGATE_H_
#define PROPAGATE_H_

#include <string.h>

#include "Node.h"
#include "Message.h"
#include "Request.h"

struct Propagate_rep: public Message_rep
{
  int id;
  int rset_size; // size in bytes of request set
  //followed by the requests
  //followed by the authenticator
};

class Propagate: public Message
{
public:
  Propagate();
  Propagate(Request *req);
  ~Propagate();

  void authenticate();
  bool verify();
  void trim(void);
  void clear();

  bool is_empty();

  bool can_add_req(int req_size);
  void add_request(Request *req);
  char* requests();

  void print(void);

  class Requests_iter
  {
    // An iterator for yielding the Requests in a Propagate message.
  public:
    Requests_iter(Propagate* m);
    // Requires: Pre_prepare is known to be valid
    // Effects: Return an iterator for the requests in "m"

    bool get(Request **req);
    // Effects: Updates "req" to "point" to the next request in the
    // Pre_prepare message and returns true. If there are no more
    // requests, it returns false.

  private:
    Propagate* msg;
    char* next_req;
  };
  friend class Requests_iter;

private:

  Propagate_rep &rep() const;
  int id() const;
};

inline Propagate_rep& Propagate::rep() const
{
  return *((Propagate_rep*) msg);
}

inline int Propagate::id() const
{
  return rep().id;
}

inline bool Propagate::can_add_req(int req_size)
{
  return ((int)sizeof(Propagate_rep) + rep().rset_size + req_size
      + node->auth_size() <= Max_message_size);
}

inline void Propagate::add_request(Request *req)
{
  memcpy(contents() + sizeof(Propagate_rep) + rep().rset_size, req->contents(),
      req->size());
  rep().rset_size += req->size();
}

inline void Propagate::trim(void)
{
  set_size(sizeof(Propagate_rep) + rep().rset_size + node->auth_size());
}

inline void Propagate::clear(void)
{
  rep().rset_size = 0;
  set_size(sizeof(Propagate_rep) + node->auth_size());
}

inline char* Propagate::requests()
{
  return contents() + sizeof(Propagate_rep);
}

inline bool Propagate::is_empty()
{
  return (rep().rset_size == 0);
}

#endif /* PROPAGATE_H_ */
