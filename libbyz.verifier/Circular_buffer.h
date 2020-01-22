/*
 * Circular_buffer.h
 *
 *  Created on: Sep 9, 2011
 *      Author: benmokhtar
 */

#ifndef CIRCULAR_BUFFER_H_
#define CIRCULAR_BUFFER_H_

#include "Message.h"
#include "Time.h"
#include <sys/eventfd.h> // eventfd()

#define PERIODIC_THR_DISPLAY (5000)

class Circular_buffer
{
public:
  Circular_buffer(int size, char *name);
  virtual ~Circular_buffer();

  bool cb_write_msg(Message*);
  // write Message* inside circualr_buffer
  // returns true if the message hase been written in the buffer
  // or false if the buffer is full

  Message* cb_read_msg();
  // reads a message from the circular buffer
  // returns a pointer to the message if there is something ready to read
  // or NULL if the buffer is empty

  Message* cb_magic();

  int fd;
private:
  int length;
  Message** circular_buffer; // circuler buffer used to pass messages from the verifier thread to the main thread
  int cb_write_index; // points to the first empty position in the buffer (ready to be written)
  int cb_read_index; // points at the oldest unread message stored in the buffer (ready to be read)
  uint64_t notif;

  // debugging purposes
  char *name; // name of this circular buffer, for debugging
  long nb_read_messages;
  long nb_write_messages;
  Time read_start_time;
  Time write_start_time;
};

inline Message* Circular_buffer::cb_magic()
{
  return (Message*) 0x12344321;
}

#endif /* CIRCULAR_BUFFER_H_ */
