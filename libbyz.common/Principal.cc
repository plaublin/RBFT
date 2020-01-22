#include <stdlib.h>
#include <strings.h>
#include "Principal.h"
#include "Node.h"
#include "Reply.h"
#include "parameters.h"

#include "crypt.h"
#include "rabin.h"
#include "umac.h"

Principal::Principal(int i, Addr a, char *p)
{
  id = i;
  addr = a;
  last_fetch = 0;

  if (p == 0)
  {
    pkey = 0;
    ssize = 0;
  }
  else
  {
    bigint b(p, 16);
    ssize = (mpz_sizeinbase2(&b) >> 3) + 1 + sizeof(unsigned);
    pkey = new rabin_pub(b);
  }

  for (int j = 0; j < 4; j++)
  {
    kin[j] = 0;
    kout[j] = 0;
  }

#ifndef USE_SECRET_SUFFIX_MD5
  ctx_in = 0;
  ctx_out = umac_new((char*) kout);
#endif

  tstamp = 0;
  my_tstamp = zeroTime();
}

Principal::~Principal()
{
  delete pkey;
}

void Principal::set_in_key(const unsigned *k)
{
  /*fprintf(stderr, "Setting a new key_in for principal %i:", id);
   for (int i = 0; i < Key_size / 4; i++)
   {
   fprintf(stderr, " %x", k[i]);
   }
   fprintf(stderr, "\n");
   fflush(stderr);
   */

  memcpy(kin, k, Key_size);

#ifndef USE_SECRET_SUFFIX_MD5
  if (ctx_in)
    umac_delete(ctx_in);
  ctx_in = umac_new((char*) kin);
#endif

}

// display the content of the context
void Principal::print_context(umac_ctx_t ctx, char *header)
{

  fprintf(stderr, "[%s] content of ctx (size=%i):", header, sizeof(*ctx));

  int* ctx_as_int = (int*) ctx;
  for (unsigned int i = 0; i < sizeof(*ctx) / 4; i++)
  {
    fprintf(stderr, " %x", ctx_as_int[i]);
  }
  fprintf(stderr, "\n");

}

/*bool Principal::verify_mac(const char *src, unsigned src_len, const char *mac,
    const char *unonce, umac_ctx_t ctx, bool print)
{
  char tag[20];

  umac(ctx, (char *) src, src_len, tag, (char *) unonce);
  bool ret = !memcmp(tag, mac, UMAC_size);

  return Message::is_mac_valid((Message_rep*)src);
}

long long Principal::umac_nonce = 0;

void Principal::gen_mac(const char *src, unsigned src_len, char *dst,
    const char *unonce, umac_ctx_t ctx, bool to_print)
{
  umac(ctx, (char *) src, src_len, dst, (char *) unonce);

  Message::set_mac_valid((Message_rep*)src);
}*/


bool Principal::verify_mac(const char *src, unsigned src_len, const char *mac,
   const char *unonce, umac_ctx_t ctx, bool print)
{
 // create a new context
/*
 umac_ctx_t fresh_ctx = umac_new((char*) kin);
 char tag[20];
 umac(fresh_ctx, (char *) src, src_len, tag, (char *) unonce);
 bool ret = !memcmp(tag, mac, UMAC_size);
 umac_delete(fresh_ctx);
 return Message::is_mac_valid((Message_rep*)src);
*/

  char tag[20];
  umac(ctx, (char *) src, src_len, tag, (char *) unonce);
  return Message::is_mac_valid((Message_rep*)src);

}

long long Principal::umac_nonce = 0;

void Principal::gen_mac(const char *src, unsigned src_len, char *dst,
   const char *unonce, umac_ctx_t ctx, bool to_print)
{
/*
 umac_ctx_t fresh_ctx = umac_new((char*) kin);
 umac(fresh_ctx, (char *) src, src_len, dst, (char *) unonce);
 umac_delete(fresh_ctx);
 Message::set_mac_valid((Message_rep*)src);
*/

  umac(ctx, (char *) src, src_len, dst, (char *) unonce);
  Message::set_mac_valid((Message_rep*)src);

}

void Principal::set_out_key(unsigned *k)
{
  /*fprintf(stderr, "Setting a new key_out for principal %i:", id);
   for (int i = 0; i < Key_size / 4; i++)
   {
   fprintf(stderr, " %x", k[i]);
   }
   fprintf(stderr, "\n");
   fflush(stderr);
   */

  memcpy(kout, k, Key_size);

  if (ctx_out)
    umac_delete(ctx_out);
  ctx_out = umac_new((char*) kout);

  tstamp = currentTime();
  my_tstamp = currentTime();
}

bool Principal::verify_signature(const char *src, unsigned src_len,
    const char *sig, bool allow_self)
{
  // Principal never verifies its own authenticator.
  if (id == node->id() && !allow_self)
    return false;

  //INCR_OP(num_sig_ver);
  //START_CC(sig_ver_cycles);

  bigint bsig;
  int s_size;
  memcpy((char*)&s_size, sig, sizeof(int));
  sig += sizeof(int);
  if (s_size + (int) sizeof(int) > sig_size())
  {
    //STOP_CC(sig_ver_cycles);
    return false;
  }

  mpz_set_raw(&bsig, sig, s_size);
#ifndef NO_CLIENT_SIGNATURES
  bool ret =
#endif
      pkey->verify(str(src, src_len), bsig);

  //STOP_CC(sig_ver_cycles);

#ifdef NO_CLIENT_SIGNATURES
  return true;
#else
  return ret;
#endif
}

unsigned Principal::encrypt(const char *src, unsigned src_len, char *dst,
    unsigned dst_len)
{
  // This is rather inefficient if message is big but messages will
  // be small.
  bigint ctext = pkey->encrypt(str(src, src_len));
  unsigned size = mpz_rawsize(&ctext);
  if (dst_len < size + 2 * sizeof(unsigned))
    return 0;

  memcpy(dst, (char*)&src_len, sizeof(unsigned));
  dst += sizeof(unsigned);
  memcpy(dst, (char*)&size, sizeof(unsigned));
  dst += sizeof(unsigned);

  mpz_get_raw(dst, size, &ctext);
  return size + 2 * sizeof(unsigned);
}

void random_nonce(unsigned *n)
{
  bigint n1 = random_bigint(Nonce_size * 8);
  mpz_get_raw((char*) n, Nonce_size, &n1);
}

int random_int()
{
  bigint n1 = random_bigint(sizeof(int) * 8);
  int i;
  mpz_get_raw((char*) &i, sizeof(int), &n1);
  return i;
}

