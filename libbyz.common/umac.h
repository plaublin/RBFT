/* umac.h */
#ifndef _UMAC_H
#define _UMAC_H

#ifdef __cplusplus
extern "C"
{
#endif




/* Following is the list of UMAC parameters supported by this code.
 * The following parameters are fixed in this implementation.
 *
 *      ENDIAN_FAVORITE_LITTLE  = 1
 *      L1-OPERATIONS-SIGN      = SIGNED   (when WORD_LEN == 2)
 *      L1-OPERATIONS-SIGN      = UNSIGNED (when WORD_LEN == 4)
 */
#define WORD_LEN                4   /* 2  | 4                             */
#define UMAC_OUTPUT_LEN         8   /* 4  | 8  | 12  | 16                 */
#define L1_KEY_LEN           1024   /* 32 | 64 | 128 | ... | 2^28         */
#define UMAC_KEY_LEN           16   /* 16 | 32                            */

/* To produce a prefix of a tag rather than the entire tag defined
 * by the above parameters, set the following constant to a number
 * less than UMAC_OUTPUT_LEN.
 */
#define UMAC_PREFIX_LEN  UMAC_OUTPUT_LEN

/* This file implements UMAC in ANSI C if the compiler supports 64-bit
 * integers. To accellerate the execution of the code, architecture-
 * specific replacements have been supplied for some compiler/instruction-
 * set combinations. To enable the features of these replacements, the
 * following compiler directives must be set appropriately. Some compilers
 * include "intrinsic" support of basic operations like register rotation,
 * byte reversal, or vector SIMD manipulation. To enable these intrinsics
 * set USE_C_AND_INTRINSICS to 1. Most compilers also allow for inline
 * assembly in the C code. To allow intrinsics and/or assembly routines
 * (whichever is faster) set only USE_C_AND_ASSEMBLY to 1.
 */
#define USE_C_ONLY            0  /* ANSI C and 64-bit integers req'd */
#define USE_C_AND_INTRINSICS  0  /* Intrinsics for rotation, MMX, etc.    */
#define USE_C_AND_ASSEMBLY    1  /* Intrinsics and assembly */

#if (USE_C_ONLY + USE_C_AND_INTRINSICS + USE_C_AND_ASSEMBLY != 1)
#error -- Only one setting may be nonzero
#endif

#define RUN_TESTS             0  /* Run basic correctness/ speed tests    */
#define HASH_ONLY             0  /* Only universal hash data, don't MAC   */

/* ---------------------------------------------------------------------- */
/* --- Primitive Data Types ---                                           */
/* ---------------------------------------------------------------------- */

#ifdef _MSC_VER
typedef char INT8; /* 1 byte   */
typedef __int16 INT16; /* 2 byte   */
typedef unsigned __int16 UINT16; /* 2 byte   */
typedef __int32 INT32; /* 4 byte   */
typedef unsigned __int32 UINT32; /* 4 byte   */
typedef __int64 INT64; /* 8 bytes  */
typedef unsigned __int64 UINT64; /* 8 bytes  */
#else
typedef char INT8; /* 1 byte   */
typedef short INT16; /* 2 byte   */
typedef unsigned short UINT16; /* 2 byte   */
typedef int INT32; /* 4 byte   */
typedef unsigned int UINT32; /* 4 byte   */
typedef long long INT64; /* 8 bytes  */
typedef unsigned long long UINT64; /* 8 bytes  */
#endif
typedef long WORD; /* Register */
typedef unsigned long UWORD; /* Register */

/* ---------------------------------------------------------------------- */
/* --- Derived Constants ------------------------------------------------ */
/* ---------------------------------------------------------------------- */

#if (WORD_LEN == 4)

typedef INT32 SMALL_WORD;
typedef UINT32 SMALL_UWORD;
typedef INT64 LARGE_WORD;
typedef UINT64 LARGE_UWORD;

#elif (WORD_LEN == 2)

typedef INT16 SMALL_WORD;
typedef UINT16 SMALL_UWORD;
typedef INT32 LARGE_WORD;
typedef UINT32 LARGE_UWORD;

#endif

/* How many iterations, or streams, are needed to produce UMAC_OUTPUT_LEN
 * and UMAC_PREFIX_LEN bytes of output
 */
#define PREFIX_STREAMS    (UMAC_PREFIX_LEN / WORD_LEN)
#define OUTPUT_STREAMS    (UMAC_OUTPUT_LEN / WORD_LEN)

/* Three compiler environments are supported for accellerated
 * implementations: GNU gcc and Microsoft Visual C++ (and copycats) on x86,
 * and Metrowerks on PowerPC.
 */
#define GCC_X86         (__GNUC__ && __i386__)      /* GCC on IA-32       */
#define MSC_X86         (_MSC_VER && _M_IX86)       /* Microsoft on IA-32 */
#define MW_PPC          ((__MWERKS__ || __MRC__) && __POWERPC__)
/* Metrowerks on PPC  */
/* ---------------------------------------------------------------------- */
/* --- Host Computer Endian Definition ---------------------------------- */
/* ---------------------------------------------------------------------- */

/* Message "words" are read from memory in an endian-specific manner.     */
/* For this implementation to behave correctly, __LITTLE_ENDIAN__ must    */
/* be set true if the host computer is little-endian.                     */

#if __i386__ || __alpha__ || _M_IX86 || __LITTLE_ENDIAN
#define __LITTLE_ENDIAN__ 1
#else
#define __LITTLE_ENDIAN__ 0
#endif

/* ---------------------------------------------------------------------- */
/* ----- RC6 Function Family Constants ---------------------------------- */
/* ---------------------------------------------------------------------- */

#define RC6_KEY_BYTES    UMAC_KEY_LEN
#define RC6_ROUNDS       20
#define RC6_KEY_WORDS    (UMAC_KEY_LEN/4)
#define RC6_TABLE_WORDS  (2*RC6_ROUNDS+4)
#define RC6_P            0xb7e15163u
#define RC6_Q            0x9e3779b9u

#define PRF_BLOCK_LEN   16                /* Constants for KDF and PDF */
#define PRF_KEY_LEN     (RC6_TABLE_WORDS*4)


/* The NH-based hash functions used in UMAC are described in the UMAC paper
 * and specification, both of which can be found at the UMAC website.
 * The interface to this implementation has two
 * versions, one expects the entire message being hashed to be passed
 * in a single buffer and returns the hash result immediately. The second
 * allows the message to be passed in a sequence of buffers. In the
 * muliple-buffer interface, the client calls the routine nh_update() as
 * many times as necessary. When there is no more data to be fed to the
 * hash, the client calls nh_final() which calculates the hash output.
 * Before beginning another hash calculation the nh_reset() routine
 * must be called. The single-buffer routine, nh(), is equivalent to
 * the sequence of calls nh_update() and nh_final(); however it is
 * optimized and should be prefered whenever the multiple-buffer interface
 * is not necessary. When using either interface, it is the client's
 * responsability to pass no more than L1_KEY_LEN bytes per hash result.
 *
 * The routine nh_init() initializes the nh_ctx data structure and
 * must be called once, before any other PDF routine.
 */

/* The "nh_aux_*" routines do the actual NH hashing work. The versions
 * prefixed with "nh_aux_hb" expect the buffers passed to them to be a
 * multiple of HASH_BUF_BYTES, allowing increased optimization. The versions
 * prefixed with "nh_aux_pb" expect buffers to be multiples of
 * L1_PAD_BOUNDARY. These routines produce output for all PREFIX_STREAMS
 * NH iterations in one call, allowing the parallel implementation of the
 * streams.
 */
#if   (UMAC_PREFIX_LEN == 2)
#define nh_aux   nh_aux_4
#elif (UMAC_PREFIX_LEN == 4)
#define nh_aux   nh_aux_8
#elif (UMAC_PREFIX_LEN == 8)
#define nh_aux   nh_aux_16
#elif (UMAC_PREFIX_LEN == 12)
#define nh_aux   nh_aux_24
#elif (UMAC_PREFIX_LEN == 16)
#define nh_aux   nh_aux_32
#endif

#define L1_KEY_SHIFT         16     /* Toeplitz key shift between streams */
#define L1_PAD_BOUNDARY      32     /* pad message to boundary multiple   */
#define ALLOC_BOUNDARY       16     /* Keep buffers aligned to this       */
#define HASH_BUF_BYTES      128     /* nh_aux_hb buffer multiple          */

/* How many extra bytes are needed for Toeplitz shift? */
#define TOEPLITZ_EXTRA       ((PREFIX_STREAMS - 1) * L1_KEY_SHIFT)

typedef struct
{
  INT8 nh_key[L1_KEY_LEN + TOEPLITZ_EXTRA]; /* NH Key */
  INT8 data[HASH_BUF_BYTES]; /* Incomming data buffer            */
  int next_data_empty; /* Bookeeping variable for data buffer.       */
  int bytes_hashed; /* Bytes (out of L1_KEY_LEN) incorperated.   */
  LARGE_UWORD state[PREFIX_STREAMS]; /* on-line state     */
} nh_ctx;




/* The final UHASH result is XOR'd with the output of a pseudorandom
 * function. Here, we use AES to generate random output and
 * xor the appropriate bytes depending on the last bits of nonce.
 * This code is optimized for sequential, increasing big-endian nonces.
 */

typedef struct
{
  INT8 cache[PRF_BLOCK_LEN];
  INT8 nonce[PRF_BLOCK_LEN];
  INT8 prf_key[PRF_KEY_LEN]; /* Expanded AES Key                       */
} pdf_ctx;

typedef struct uhash_ctx
{
  nh_ctx hash; /* Hash context for L1 NH hash    */
  /* Extra stuff for the WORD_LEN == 2 case, where a polyhash tansition
   * may occur between p32 and p64
   */
#if (WORD_LEN == 2)
  UINT32 poly_key_4[PREFIX_STREAMS]; /* p32 Poly keys                   */
  UINT64 poly_store[PREFIX_STREAMS]; /* To buffer NH-16 output for p64  */
  int poly_store_full; /* Flag for poly_store             */
  UINT32 poly_invocations; /* Number of p32 words hashed      */
#endif
  UINT64 poly_key_8[PREFIX_STREAMS]; /* p64 poly keys                */
  UINT64 poly_accum[PREFIX_STREAMS]; /* poly hash result             */
  LARGE_UWORD ip_keys[PREFIX_STREAMS * 4];/* Inner-product keys           */
  SMALL_UWORD ip_trans[PREFIX_STREAMS]; /* Inner-product translation    */
  UINT32 msg_len; /* Total length of data passed to uhash */
} uhash_ctx;

/* The UMAC interface has two interfaces, an all-at-once interface where
 * the entire message to be authenticated is passed to UMAC in one buffer,
 * and a sequential interface where the message is presented a bit at a time.
 * The all-at-once is more optimaized than the sequential version and should
 * be preferred when the sequential interface is not required.
 */
typedef struct umac_ctx
{
  uhash_ctx hash; /* Hash function for message compression    */
  pdf_ctx pdf; /* PDF for hashed output                    */
} umac_ctx;

typedef struct umac_ctx *umac_ctx_t;

umac_ctx_t umac_new(char key[]);
/* Dynamically allocate a umac_ctx struct, initialize variables, 
 * generate subkeys from key.
 */

int umac_reset(umac_ctx_t ctx);
/* Reset a umac_ctx to begin authenicating a new message */

int umac_update(umac_ctx_t ctx, char *input, long len);
/* Incorporate len bytes pointed to by input into context ctx */

int umac_final(umac_ctx_t ctx, char tag[], char nonce[8]);
/* Incorporate any pending data and the ctr value, and return tag. 
 * This function returns error code if ctr < 0. 
 */

int umac_delete(umac_ctx_t ctx);
/* Deallocate the context structure */

int umac(umac_ctx_t ctx, char *input, long len, char tag[], char nonce[8]);
/* All-in-one implementation of the functions Reset, Update and Final */

/* uhash.h */

typedef struct uhash_ctx *uhash_ctx_t;
/* The uhash_ctx structure is defined by the implementation of the    */
/* UHASH functions.                                                   */

uhash_ctx_t uhash_alloc(char key[16]);
/* Dynamically allocate a uhash_ctx struct and generate subkeys using */
/* the kdf and kdf_key passed in. If kdf_key_len is 0 then RC6 is     */
/* used to generate key with a fixed key. If kdf_key_len > 0 but kdf  */
/* is NULL then the first 16 bytes pointed at by kdf_key is used as a */
/* key for an RC6 based KDF.                                          */

int uhash_free(uhash_ctx_t ctx);

int uhash_set_params(uhash_ctx_t ctx, void *params);

int uhash_reset(uhash_ctx_t ctx);

int uhash_update(uhash_ctx_t ctx, char *input, long len);

int uhash_final(uhash_ctx_t ctx, char ouput[]);

int uhash(uhash_ctx_t ctx, char *input, long len, char output[]);

#ifdef __cplusplus
}
#endif

#endif
