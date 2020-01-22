#ifndef _Data_h
#define _Data_h 1

#include "types.h"
#include "Message.h"
#include "Partition.h"
#include "State_defs.h"



// 
// Data messages have the following format:
//
struct Data_rep : public Message_rep {
  int index;        // index of this page within level
  int padding;
  Seqno lm;         // Seqno of last checkpoint in which data was modified

  char data[Block_size];

};

class Data : public Message {
  // 
  // Data messages
  //
public:
  Data(int i, Seqno lm, char *data);
  // Effects: Creates a new Data message. 
  //          (if we are using BASE) "totalsz" is the size of the object,
  //          chunkn is the number of the fragment that we are sending
  //          and "data" points to the beginning of the fragment (not to
  //          the beginning of the object - XXX change this?)

  int index() const;
  // Effects: Returns index of data page

  Seqno last_mod() const;
  // Effects: Returns the seqno of last checkpoint in which data was
  // modified


  char *data() const;
  // Effects: Returns a pointer to the data page.

  static bool convert(Message *m1, Data *&m2);
  // Effects: If "m1" has the right size and tag, casts "m1" to a
  // "Data" pointer, returns the pointer in "m2" and returns
  // true. Otherwise, it returns false. 

private:
  Data_rep &rep() const;
  // Effects: Casts contents to a Data_rep&
};

inline Data_rep &Data::rep() const { 
  return *((Data_rep*)msg); 
}

inline int Data::index() const { return rep().index; }

inline Seqno Data::last_mod() const { return rep().lm; }


inline char *Data::data() const { return rep().data; }


#endif // _Data_h
