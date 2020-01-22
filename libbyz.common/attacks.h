#ifndef __ATTACKS_H_
#define __ATTACKS_H_

#include "Node.h"
#include "Request.h"

extern Node* node;

// [attacks] API.
#define NONE 0
#define FLOOD_PROT   1
#define FLOOD_MAX    2
#define CLIENT_BIAS  4
bool clientBias();
bool floodMax();
bool floodProtocol();


// [attack] Attacks.
// We will fix the attacks to specific replicas.
// Only the first primary biases on the first client,
// Only the last primary floods (either max or protocol).
extern int attack_mode;

inline void setAttack(int mode)
{
  if (mode == NONE) return;
  switch (mode) {
  case FLOOD_MAX:
    attack_mode |= mode;
    fprintf(stderr, "Attacking with flood max.\n");
    break;
  case FLOOD_PROT:
    attack_mode |= mode;
    fprintf(stderr, "Attacking with flood protocol.\n");
    break;
  case CLIENT_BIAS:
    attack_mode |= mode;
    fprintf(stderr, "Attacking with client bias.\n");
    break;
  default:
    break;
  }
}



inline bool clientBias(Request *m)
{
  return (attack_mode & CLIENT_BIAS) && m->client_id() == node->n() && node->id() == 0;
}

inline bool floodMax() 
{
  return (attack_mode & FLOOD_MAX);
}

inline bool floodProtocol()
{
  return (attack_mode & FLOOD_PROT) && node->id() == 3;
}

#endif
