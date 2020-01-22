#include <stdio.h>

#include "Protocol_instance_change.h"
#include "Message_tags.h"
#include "Node.h"

Protocol_instance_change::Protocol_instance_change(View change_id) :
      Message(Protocol_instance_change_tag,
          sizeof(Protocol_instance_change_rep) + node->auth_size())
{
  rep().sender_id = node->id();
  rep().change_id = change_id;
}

Protocol_instance_change::~Protocol_instance_change()
{
}

void Protocol_instance_change::authenticate()
{
  node->gen_auth_out(contents(), sizeof(Protocol_instance_change_rep));
}

bool Protocol_instance_change::verify()
{
  // Protocol_instance_change must be sent by replicas.
  if (!node->is_replica(sender_id()) || sender_id() == node->id())
    return false;

  // Check Mac size.
  if (size() - (int) sizeof(Protocol_instance_change_rep) < node->auth_size(sender_id()))
    return false;

  return node->verify_auth_in(sender_id(), contents(), sizeof(Protocol_instance_change_rep));
}
