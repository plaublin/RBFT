#ifndef PROTOCOL_INSTANCE_CHANGE_H_
#define PROTOCOL_INSTANCE_CHANGE_H_

#include <string.h>

#include "Node.h"
#include "Message.h"
#include "types.h"

struct Protocol_instance_change_rep: public Message_rep
{
  int sender_id;
  View change_id; // protocol instance change id
  //followed by the authenticator
};

class Protocol_instance_change: public Message
{
public:
  Protocol_instance_change(View change_id);
  virtual ~Protocol_instance_change();

  void authenticate();
  bool verify();

  int sender_id() const;
  View change_id() const;

private:
  Protocol_instance_change_rep &rep() const;

};

inline Protocol_instance_change_rep& Protocol_instance_change::rep() const
{
  return *((Protocol_instance_change_rep*) msg);
}

inline int Protocol_instance_change::sender_id() const
{
  return rep().sender_id;
}

inline View Protocol_instance_change::change_id() const
{
  return rep().change_id;
}

#endif /* PROTOCOL_INSTANCE_CHANGE_H_ */
