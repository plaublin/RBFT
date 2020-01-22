#ifndef _State_h
#define _State_h 1

#include "th_assert.h"
#include "types.h"
#include "Digest.h"
#include "Bitmap.h"
#include "Partition.h"
#include "Log.h"
#include "Time.h"
#include "State_defs.h"

//
// Auxiliary classes:
//
class Block;
class Part;
class FPartQueue;
class CPartQueue;
class Checkpoint_rec;
class Data;
class Meta_data;
class Meta_data_d;
class Fetch;
class PIR;
class Meta_data_cert;
class PageCache;
struct DSum;


class State {
public:

  State(PIR *replica, char *memory, int num_bytes);
  // Requires: mem is Block aligned and contains an integral number of
  // Blocks.
  // Effects: Creates an object that handles state digesting and
  // checkpointing for the region starting at "mem" with size
  // "num_bytes".


  ~State();
  // Effects: Deallocates all storage associated with state.

  //
  // Maintaining checkpoints
  //
  void cow_single(int bindex);
  // Effects: Copies block with bindex and marks it as copied.

  void cow(char *mem, int size);
  // Effects: Performs copies for the blocks in the region
  // starting at "mem" and of size "size"
  // if they have not been copied since last checkpoint.
  // It also marks them as copied.
 
  void checkpoint(Seqno seqno);
  // Effects: Saves a checkpoint of the current state (associated with
  // seqno) and computes the digest of all partition.

  Seqno rollback();
  // Requires: !in_fetch_state && there is a checkpoint in this
  // Effects: Rolls back to the last checkpoint and returns its
  // sequence number.

  void discard_checkpoint(Seqno seqno, Seqno le);
  // Effects: Discards the checkpoints with sequence number less than
  // or equal to "seqno" and saves information about the current state 
  // whose sequence number is "le"

  void compute_full_digest();
  // Effects: Computes a state digest from scratch and a digest for
  // each partition.
  
  bool digest(Seqno n, Digest& d);
  // Effects: If there is a checkpoint for sequence number "n" in
  // this, returns true and sets "d" to its digest. Otherwise, returns
  // false.

  // 
  // Fetching missing state
  //
  bool in_fetch_state() const;
  // Effects: Returns true iff the replica is fetching missing state.

  void start_fetch(Seqno last_exec, Seqno c=-1, Digest *cd=0, bool stable=false);
  // Effects: Sends fetch message for missing state. If "c != -1" then
  // "cd" points to the digest of checkpoint sequence number "c". "stable" should
  // be true iff the specific checkpoint being fetched is stable.

  void send_fetch(bool change_replier=false);
  // Effects: Sends fetch message requesting missing state. If
  // change_replier is true changes the selected replier.

  void start_check(Seqno last_exec);
  // Effects: Starts checking state that reflects execution up to "last_exec" 

  bool shutdown(FILE* o, Seqno ls);
  // Effects: Shuts down state writing value to "o"

  bool restart(FILE* i, PIR *rep, Seqno ls, Seqno le, bool corrupt); 
  // Effects: Restarts the state object from value in "i"

  bool enforce_bound(Seqno b, Seqno ks, bool corrupt);
  // Effects: Enforces that there is no information above bound
  // "b". "ks" is the maximum sequence number that I know is stable.

  // Message handlers
  void handle(Meta_data *m);
  void handle(Meta_data_d *m);
  void handle(Data *m);

  bool handle(Fetch *m, Seqno last_stable);
  // Effects: Returns true if it was able to verify the message and
  // false otherwise.

  void mark_stale();
  // Effects: Discards incomplete certificate.

  void simulate_reboot();
  // Effects: Simulates a reboot by invalidating state

  bool retrans_fetch(Time cur) const;
  // Effects: Returns true iff fetch should be retransmitted
  bool retrans_fetch() const;
 
  
private:
  // Parent replica object.
  PIR* replica;

  // Actual memory holding current state and the number of Blocks
  // in that memory.

  Block* mem;
  int nb;

  // Bitmap with a bit for each block in the memory region indicating
  // whether the block should be copied the next time it is written;
  // blocks should be copied iff their bit is 0.
  Bitmap cowb;
  
  Part* ptree[PLevels];        // Partition tree.
  DSum* stree[PLevels-1];      // Tree of sums of digests of subpartitions.

  Log<Checkpoint_rec> clog;   // Checkpoint log
  Seqno lc;                   // Sequence number of the last checkpoint


  //
  // Information used while fetching state.
  //
  bool fetching;      // true iff replica is fetching missing state
  bool keep_ckpts;    // whether to keep last checkpoints
  int flevel;         // level of state partition info being fetched  
  FPartQueue* stalep[PLevels]; // queue of out-of-date partitions for each level

    
  Meta_data_cert* cert;  // certificate for partition we are working on
  int lreplier;          // id of last replica we chose as replier
  Time last_fetch_t;     // Time when last fetch was sent. 


  //
  // Information used while checking state during recovery
  //
  bool checking;       // true iff replica is checking state
  Seqno check_start;   // last checkpoint sequence number when checking started
  bool corrupt;        // true iff replica's state is known to be corrupt
  int poll_cnt;        // check for messages after digesting this many blocks

  // queue of partitions whose digests need to be checked. It can have
  // partitions from different levels.
  CPartQueue *to_check;   
  int lchecked;      // index of last block checked in to_check.high().
  int refetch_level; // level of ancestor of current partition whose subpartitions 
                     // have already been added to to_check.


  int digest(Digest &d, int l, int i);
  // Effects: Sets "d" to the current digest of partition  "(l,i)"
  // Returns: size of object in partition (l,i)

  void digest(Digest& d, int i, Seqno lm, char *data, int size);
  // Effects: Sets "d" to MD5(i#lm#(data,size))

  bool check_digest(Digest& d, Meta_data* m);
  // Effects: Checks if the digest of the partion in "m" is "d"

  void done_with_level();
  // Requires: flevel has an empty out-of-date queue.
  // Effects: It decrements flevel and, if parent is consistent,
  // removes parent. If the queue of parent becomes empty it calls
  // itself recursively. Unless there is no parent, in which case it
  // in_fetch_state_to false and updates state accordingly.

  void update_ptree(Seqno n);
  // Effects: Updates the digests of the blocks whose cow bits were reset
  // since the last checkpoint and computes a new state digest using the
  // state digest computed during the last checkpoint.

  char* get_data(Seqno c, int i);

  // Requires: There is a checkpoint with sequence number "c" in this
  // Effects: Returns a pointer to the data for block index "i" at
  // checkpoint "c". [objsz gets the size of the object]

  Part& get_meta_data(Seqno c, int l, int i);
  // Requires: There is a checkpoint with sequence number "c" in this
  // Effects: Returns a pointer to the information for partition "(l,i)" at
  // checkpoint "c".

  bool check_data(int i);
  // Effects: Checks whether the actual digest of block "i" and its
  // digest in the ptree match.

};

inline bool State::in_fetch_state() const { return fetching; }

inline bool State::retrans_fetch(Time cur) const { 
  return fetching && diffTime(cur, last_fetch_t) > 100000;
}

inline bool State::retrans_fetch() const { 
  return fetching;
}

#endif // _State_h



