/*
 * blocking_circular_buffer.h
 *
 *  Created on: 3 mai 2011
 *      Author: benmokhtar
 */

#ifndef BLOCKING_CIRCULAR_BUFFER_H_
#define BLOCKING_CIRCULAR_BUFFER_H_

#include "Message.h"
#include "Time.h"
#include <stdio.h>
#include <pthread.h>

#define PERIODIC_THR_DISPLAY (5000)

class Blocking_circular_buffer
{
public:
  Blocking_circular_buffer(int size, char *name);
  virtual ~Blocking_circular_buffer();

  int bcb_write_msg(Message *m);
  Message* bcb_read_msg();

  // return the number of messages in the circular buffer
  int bcb_nb_msg();

  Message* bcb_magic();

private:
  Message **cb;
  int length;
  int read_idx;
  int write_idx;
  int nb_msg;
  pthread_mutex_t mutex;
  pthread_cond_t cond_can_write;
  pthread_cond_t cond_can_read;

  // debugging purposes
  char *name; // name of this circular buffer, for debugging
  long nb_read_messages;
  long nb_write_messages;
  Time read_start_time;
  Time write_start_time;
};

// return the number of messages in the circular buffer
inline int Blocking_circular_buffer::bcb_nb_msg()
{
  return nb_msg;
}

inline Message* Blocking_circular_buffer::bcb_magic()
{
  return (Message*) 0x12344321;
}

#endif /* BLOCKING_CIRCULAR_BUFFER_H_ */

