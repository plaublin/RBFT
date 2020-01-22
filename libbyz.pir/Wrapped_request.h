#ifndef _Wrapped_request_h
#define _Wrapped_request_h 1

#include "types.h"
#include "Digest.h"
#include "Prepare.h"
#include "Message.h"
#include "Request.h"
#include "Req_list.h"

class Principal;
class Req_queue;
class Prepare;
class Request;
class Pre_prepare;

// 
// Pre_prepare messages have the following format:
//
struct Wrapped_request_rep : public Message_rep {
  int view;           // used to store the client id
  Seqno seqno;         
  Digest digest;       // digest of request set concatenated with 
                       // big reqs and non-deterministic choices
  int rset_size;       // size in bytes of request set
  short n_big_reqs;    // number of big requests
  short non_det_size;  // size in bytes of non-deterministic choices
  
  // Followed by "rset_size" bytes of the request set, "n_big_reqs"
  // Digest's, "non_det_size" bytes of non-deterministic choices, and
  // a variable length signature in the above order.
};


class Wrapped_request : public Message {
  // 
  // Pre_prepare messages
  //
public:
  Wrapped_request() : Message() {}

  Wrapped_request(View v, Seqno s, Req_queue &reqs, bool faultyClient=false);
  // Effects: Creates a new signed Pre_prepare message with view
  // number "v", sequence number "s", the requests in "reqs" (up to a
  // maximum size) and appropriate non-deterministic choices.  It
  // removes the elements of "reqs" that are included in the message
  // from "reqs" and deletes them.
  // If faultyClient==true then the wrapped request will have a wrong MAC

  Wrapped_request(View v, Seqno s, Pre_prepare *pp);
  // Effects: Creates a Wrapped_request that will contain all the requests present
  // in the Pre_prepare pp. It will be with view number "v" and sequence number "s".

  char* choices(int &len);
  // Effects: Returns a buffer that can be filled with non-deterministic choices

/*
  Wrapped_request* clone(View v) const;
  // Effects: Creates a new object with the same state as this but view v.
*/

  void re_authenticate(Principal *p=0);
  // Effects: Recomputes the authenticator in the message using the most
  // recent keys. If "p" is not null, may only update "p"'s
  // entry.

  int client_id() const;
  // Effects: Fetches the view number from the message.

  Seqno seqno() const;
  // Effects: Fetches the sequence number from the message.

  int id() const;
  // Effects: Returns the identifier of the primary for view() (which is
  // the replica that sent the message if the message is correct.)

  bool match(const Prepare *p) const;
  // Effects: Returns true iff "p" and "this" match.

  Digest& digest() const;
  // Effects: Fetches the digest from the message.

  int request_set_size();

  char* requests();
  // Effects: Returns a pointer to the first request contents.
 
 class Requests_iter {
    // An iterator for yielding the Requests in a Pre_prepare message.
    // Requires: A Pre_prepare message cannot be modified while it is
    // being iterated on and all the big requests referred to by "m"
    // must be cached.
  public:
    Requests_iter(Wrapped_request* m);
    // Requires: Pre_prepare is known to be valid
    // Effects: Return an iterator for the requests in "m"
	
    bool get(Request& req);
    // Effects: Updates "req" to "point" to the next request in the
    // Pre_prepare message and returns true. If there are no more
    // requests, it returns false.

  private:
    Wrapped_request* msg; 
    char* next_req;
    int big_req;
  };
  friend  class Requests_iter;

  // Maximum number of big reqs in pre-prepares.
  static const int big_req_max = sizeof(BR_map)*8;  
  int num_big_reqs() const;
  // Effects: Returns the number of big request digests in this
  
  Digest& big_req_digest(int i);
  // Requires: 0 <= "i" < "num_big_reqs()"
  // Effects: Returns the digest of the i-th big request in this

  static const int NAC = 1;
  static const int NRC = 2;
  bool verify_MAC(int mode=0);
  bool verify_request(int mode=0);
  // Effects: If "mode == 0", verifies if the message is authenticated
  // by the replica "id()", if the digest is correct, and if the
  // requests are authentic. If "mode == NAC", it performs all checks
  // except that it does not check if the message is authenticated by
  // the replica "id()". If "mode == NRC", it performs all checks
  // except that it does not verify the authenticity of the requests.


  static bool convert(Message* m1, Wrapped_request*& m2);
  // Effects: If "m1" has the right size and tag, casts "m1" to a
  // "Pre_prepare" pointer, returns the pointer in "m2" and returns
  // true. Otherwise, it returns false. 

private:
  Wrapped_request_rep& rep() const;
  // Effects: Casts contents to a Pre_prepare_rep&


  Digest* big_reqs();
  // Effects: Returns a pointer to the first digest of a big request
  // in this.

  char* non_det_choices();
  // Effects: Returns a pointer to the buffer with non-deterministic
  // choices.
};

inline Wrapped_request_rep& Wrapped_request::rep() const { 
  return *((Wrapped_request_rep*)msg); 
}

inline char* Wrapped_request::requests() { 
  char *ret = contents()+sizeof(Wrapped_request_rep);
  return ret; 
}

inline  Digest* Wrapped_request::big_reqs() {
  char *ret = requests()+rep().rset_size;
  return (Digest*)ret;
}

inline char* Wrapped_request::non_det_choices() {
  char *ret = ((char*)big_reqs())+rep().n_big_reqs*sizeof(Digest);
  return ret;
}

inline char* Wrapped_request::choices(int &len) {
  len = rep().non_det_size;
  return non_det_choices();
}

inline int Wrapped_request::client_id() const { return rep().view; }

inline Seqno Wrapped_request::seqno() const { return rep().seqno; }

inline bool Wrapped_request::match(const Prepare* p) const {
  th_assert(view() == p->view() && seqno() == p->seqno(), "Invalid argument");
  return digest() == p->digest();
}

inline Digest& Wrapped_request::digest() const { return rep().digest; }

inline int Wrapped_request::num_big_reqs() const { return rep().n_big_reqs; }
 
inline Digest& Wrapped_request::big_req_digest(int i) {
  th_assert(i >= 0 && i < num_big_reqs(), "Invalid argument");
  return *(big_reqs()+i);
}

inline int Wrapped_request::request_set_size() {
  return rep().rset_size;
}

#endif // _Pre_prepare_h
