/*
 * Node_blacklister.h
 */

#ifndef NODE_BLACKLISTER_H_
#define NODE_BLACKLISTER_H_

class Node_blacklister
{
public:
  // n is the number of nodes that use the blacklister
  // s is the size of the sliding window
  // p is the reading on blacklisted nodes period
  Node_blacklister(int n, int s, int p);
  ~Node_blacklister();

  void add_message(int id, bool valid);
  bool is_blacklisted(int id);

  void new_reading_iteration(void);
  bool time_to_read_from_blacklisted(void);
  void blacklist_node(int id);
  void unblacklist_node(int id);

private:
  // 1 such struct per node
  struct node_stat
  {
    int cpt; // the counter, to know if the node has to be blacklisted or not
    char *sliding_window; // the sliding window used to know if the node is blacklisted or not
    int current_pos; // to manage the sliding window
  };

  int num_nodes;
  int sliding_window_size;
  int reading_blacklisted_period;
  int current_reading_iteration;
  struct node_stat *node_stats;
};

inline bool Node_blacklister::is_blacklisted(int id)
{
#ifdef NO_BLACKLISTING
  return false;
#else
  return (node_stats[id].cpt < 0);
#endif
}

inline void Node_blacklister::new_reading_iteration(void)
{
  if (++current_reading_iteration == reading_blacklisted_period)
  {
    current_reading_iteration = 0;
  }
}

inline bool Node_blacklister::time_to_read_from_blacklisted(void)
{
#ifdef NO_BLACKLISTING
  return true;
#else
  return (current_reading_iteration == 0);
#endif
}

#endif /* NODE_BLACKLISTER_H_ */
