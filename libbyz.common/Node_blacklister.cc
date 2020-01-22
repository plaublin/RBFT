/*
 * Node_blacklister.cc
 */

#include <stdio.h>

#include "Node_blacklister.h"
#include "parameters.h"

// n is the number of nodes that use the blacklister
// s is the size of the sliding window
// p is the reading on blacklisted nodes period
Node_blacklister::Node_blacklister(int n, int s, int p)
{
  num_nodes = n;
  sliding_window_size = s;
  reading_blacklisted_period = p;
  current_reading_iteration = 0;
  node_stats = new node_stat[num_nodes];

  for (int i = 0; i < num_nodes; i++)
  {
    node_stats[i].cpt = 0;
    node_stats[i].current_pos = 0;
    node_stats[i].sliding_window = new char[sliding_window_size];

    for (int j = 0; j < sliding_window_size; j++)
    {
      node_stats[i].sliding_window[j] = 0;
    }
  }
}

Node_blacklister::~Node_blacklister()
{
  delete [] node_stats;
}

void Node_blacklister::add_message(int id, bool valid)
{
  char v = (valid ? 1 : -1);
  node_stats[id].sliding_window[node_stats[id].current_pos] = v;
  node_stats[id].current_pos = (node_stats[id].current_pos + 1)
      % sliding_window_size;

//#ifdef MSG_DEBUG
  bool was_blacklisted = is_blacklisted(id);
//#endif

  // add the new value, and remove the oldest one
  node_stats[id].cpt += v
      - node_stats[id].sliding_window[node_stats[id].current_pos];

//#ifdef MSG_DEBUG
  if (!was_blacklisted && is_blacklisted(id))
  {
    fprintf(stderr, "Node %d is blacklisted\n", id);
  }
  else if (was_blacklisted && !is_blacklisted(id))
  {
    fprintf(stderr, "Node %d is no longer blacklisted\n", id);
  }
//#endif
}

void Node_blacklister::blacklist_node(int id)
{
  for (int j = 0; j < sliding_window_size; j++)
  {
    node_stats[id].sliding_window[j] = -1;
  }
  node_stats[id].cpt = -sliding_window_size;

//#ifdef MSG_DEBUG
  fprintf(stderr, "Blacklisting node %d\n", id);
//#endif
}

void Node_blacklister::unblacklist_node(int id)
{
  for (int j = 0; j < sliding_window_size; j++)
  {
    node_stats[id].sliding_window[j] = 1;
  }

  node_stats[id].cpt = sliding_window_size;

//#ifdef MSG_DEBUG
  fprintf(stderr, "Unblacklisting node %d\n", id);
//#endif
}
