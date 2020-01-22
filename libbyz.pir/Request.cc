#include <stdlib.h>
#include <strings.h>
#include "th_assert.h"
#include "Message_tags.h"
#include "Request.h"
#include "Node.h"
#include "Principal.h"
#include "MD5.h"

#include "Statistics.h"
#include "parameters.h"

#ifdef SIMULATE_SIGS 

// the following block of code is used to
// simulate signing signatures at the replicas	
// upon receipt of request messages from the	
// client					
#include "crypt.h"
#include "rabin.h"
#include "umac.h"

long sig_count = 0;

char pk1[1024] = "d3bf9ada150474e93d21a4818ccf40e97df94f565c0528973a7799fc3e9ee69e0561fff15631850e2c5b8f9accee851cfc170cd0193052d4f75dfee18ab1d24b";

char pk2[1024] = "e7b8885fa504355a686140181ae956e726e490ac2f905e52a78bea2ef16acef31788b827f35f0de1343766e6f2cbe44f436d7e5eceeb67791ccd296422fb50ef";

char pk3[1024] = "bfaa873efc926cb91646a89e45f96582041e3eed35cde0ef60b5c006cfad883781ee807411b0df3c74dc3ebbbce59c21d67711c83ecf596357c23dba33da338fb5577179a3b6188c59590aa1301eb852c0e14fa9225c0b377fee944eb9fa110ad7a316269e4b13b153887426a347c7c3c5feb1e3107bac4c6e29327b3343c405";

rabin_priv *priv_key = new rabin_priv(bigint(pk1,16), bigint(pk2,16));
rabin_pub *pub_key = new rabin_pub(bigint(pk3,16));
const str _msg = "Hello world, how are you doing today?aaferwh";
bigint sig;

void setupSignatureSimulation()
{
  sig = priv_key->sign(_msg);
}

#endif

// extra & 1 = read only
// extra & 2 = signed

Request::Request(Request_id r, short rr) :
  Message(Request_tag, Max_message_size)
{
  //fprintf(stderr, "creating a request with rid %qd\n", r);
  rep().cid = node->id();
  rep().rid = r;
  rep().replier = rr;
  rep().command_size = 0;
  set_size(sizeof(Request_rep));
}

Request* Request::clone() const
{
  //   Request* ret = (Request*)new Message(max_size);
  Request* ret = (Request*) new Message(msg->size);
  memcpy(ret->msg, msg, msg->size);
  return ret;
}

char *Request::store_command(int &max_len)
{
  int max_auth_size = MAX(node->principal()->sig_size(), node->auth_size());
  max_len = msize() - sizeof(Request_rep) - max_auth_size;
  return contents() + sizeof(Request_rep);
}

inline void Request::comp_digest(Digest& d)
{
  //INCR_OP(num_digests);
  //START_CC(digest_cycles);

  MD5_CTX context;
  MD5Init(&context);
  MD5Update(&context, (char*) &(rep().cid), sizeof(int) + sizeof(Request_id)
      + rep().command_size);
  MD5Final(d.udigest(), &context);

  /*
   * code for printing the content from which the MAC is generated
   char string2print[512];
   int i;

   int len = 0;
   for (i = 0; i < sizeof(int) + sizeof(Request_id) + rep().command_size; i++)
   {
   sprintf(string2print + len, "%x ", ((char*) &(rep().cid))[i]);
   len = strlen(string2print);
   }

   fprintf(stderr, "Client %i, req %qd, content=[ %s]\n", ((Client*) node)->id(),
   ((Client*) node)->get_rid(), string2print);
   */

  //STOP_CC(digest_cycles);
}

void Request::authenticate(int act_len, bool read_only, bool faultyClient)
{
  th_assert((unsigned)act_len <=
      msize()-sizeof(Request_rep)-node->auth_size(),
      "Invalid request size");

  rep().extra = ((read_only) ? 1 : 0);
  rep().command_size = act_len;
  if (rep().replier == -1)
    rep().replier = lrand48() % node->n();
  comp_digest(rep().od);

  int old_size = sizeof(Request_rep) + act_len;
  set_size(old_size + node->auth_size());
  node->gen_auth_in(contents(), sizeof(Request_rep), contents() + old_size,
      faultyClient);
}

void Request::re_authenticate(bool change, Principal *p, bool faultyClient)
{
  if (change)
  {
    rep().extra &= ~1;
  }
  int new_rep = lrand48() % node->n();
  rep().replier = (new_rep != rep().replier) ? new_rep : (new_rep + 1)
      % node->n();

  int old_size = sizeof(Request_rep) + rep().command_size;
  if ((rep().extra & 2) == 0)
  {
    node->gen_auth_in(contents(), sizeof(Request_rep), contents() + old_size,
        faultyClient);
  }
  else
  {
    node->gen_signature(contents(), sizeof(Request_rep), contents() + old_size);
  }
}

void Request::sign(int act_len)
{
  th_assert((unsigned)act_len <=
      msize()-sizeof(Request_rep)-node->principal()->sig_size(),
      "Invalid request size");

  rep().extra |= 2;
  rep().command_size = act_len;
  comp_digest(rep().od);

  int old_size = sizeof(Request_rep) + act_len;
  set_size(old_size + node->principal()->sig_size());
  node->gen_signature(contents(), sizeof(Request_rep), contents() + old_size);
}

Request::Request(Request_rep *contents) :
  Message(contents)
{
}

void Request::corrupt(bool faultyPrimary)
{
#ifdef SIGN_REQUESTS
  return;
#endif
  if (!faultyPrimary || // primary is not faulty
      ((rep().extra & 2) != 0)) // or request does not have an authenticator
    return; // short circuit out if the primary is not faulty or no authenticators

  // dest address == start address of contents + how big the contents
  // are + command size
  char *dest = contents() + sizeof(Request_rep) + rep().command_size;

#ifdef USE_SECRET_SUFFIX_MD5
  int = MAC_size;
#else
  dest += UNonce_size;
  int offset = UMAC_size;
#endif
  int num_replicas = node->n();
  int node_id = node->id();
  for (int i = 0; i < num_replicas; i++)
  {
    if (i != node_id)
      dest[0]++;
    dest += offset;
  }

}

bool Request::verify_without_digest()
{
  const int nid = node->id();
  const int cid = client_id();
  const int old_size = sizeof(Request_rep) + rep().command_size;
  Principal* p = node->i_to_p(cid);

  if (p != 0)
  {
    if (cid != nid && cid >= node->n() && size() - old_size >= node->auth_size(cid)) {
      return node->verify_auth_out(cid, contents(), sizeof(Request_rep), contents() + old_size);
    }
  }
  return false;
}

bool Request::verify()
{
  const int nid = node->id();
  const int cid = client_id();
  const int old_size = sizeof(Request_rep) + rep().command_size;
  Principal* p = node->i_to_p(cid);
  Digest d;

  comp_digest(d);
  if (p != 0 && d == rep().od)
  {
    if ((rep().extra & 2) == 0)
    {
      // Message has an authenticator.

#ifdef SIMULATE_SIGS
      //      sig_count++;
      pub_key->verify(_msg, sig);
      //      if (sig_count % 100000 == 0)
      //	fprintf(stderr, "sig count: %ld\n", sig_count);
#endif

      if (cid != nid && cid >= node->n() && size() - old_size
          >= node->auth_size(cid))
        return node->verify_auth_out(cid, contents(), sizeof(Request_rep),
            contents() + old_size);
    }
    else
    {
      // Message is signed.
      if (size() - old_size >= p->sig_size())
        return p->verify_signature(contents(), sizeof(Request_rep), contents()
            + old_size, true);
    }
  }
  return false;
}

bool Request::convert(Message *m1, Request *&m2)
{
  if (!m1->has_tag(Request_tag, sizeof(Request_rep)))
    return false;

  m2 = (Request*) m1;
  m2->trim();
  return true;
}

bool Request::convert(char *m1, unsigned max_len, Request &m2)
{
  if (!Message::convert(m1, max_len, Request_tag, sizeof(Request_rep), m2))
    return false;
  return true;
}

