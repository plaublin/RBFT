#include <string.h>
#include "th_assert.h"
#include "Message_tags.h"
#include "Data.h"

Data::Data(int i, Seqno lm, char *data)
: Message(Data_tag, sizeof(Data_rep)) {
  rep().index = i;
  rep().padding = 0;
  rep().lm = lm;

  // TODO: Avoid this copy using sendmsg with iovecs.
  memcpy(rep().data, data, Block_size);                 

} 


bool Data::convert(Message *m1, Data  *&m2) {
  if (!m1->has_tag(Data_tag, sizeof(Data_rep)))
    return false;

  m2 = (Data*)m1;
  m2->trim();
  return true;
}
