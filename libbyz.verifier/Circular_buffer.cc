/*
 * Circular_buffer.cpp
 *
 *  Created on: Sep 9, 2011
 *      Author: benmokhtar
 */

#include <stdio.h>

#include "Circular_buffer.h"
#include "Message.h"

Circular_buffer::Circular_buffer(int size, char *name)
{
  length = size;
  circular_buffer = new Message*[length];
  for (int i = 0; i < size; i++)
    circular_buffer[i] = cb_magic();
  cb_write_index = 0;
  cb_read_index = 0;
  fd = eventfd(0, EFD_SEMAPHORE);

  this->name = name;
  nb_read_messages = 0;
  nb_write_messages = 0;
  read_start_time = currentTime();
  write_start_time = currentTime();
}

Circular_buffer::~Circular_buffer()
{
  delete[] circular_buffer;
  close(fd);
}

bool Circular_buffer::cb_write_msg(Message* message)
{
  //fprintf(stderr, "[%s] Writing --- ri: %d, wi: %d \n", name, cb_read_index, cb_write_index);
  if (((cb_write_index + 1) % length) == cb_read_index)
    return false; //the buffer is full

  if (circular_buffer[cb_write_index] != cb_magic()) {
    fprintf(stderr, "Going to erase an existing message: %p at %d\n", circular_buffer[cb_write_index], cb_write_index);
    return false;
  }

  //the buffer is not full
  //fprintf(stderr, "[%s] cb_write_msg: %p at %d\n", name, message, cb_write_index);
  circular_buffer[cb_write_index] = message;
  cb_write_index = (cb_write_index + 1) % length;
  notif = 1;
  write(fd, &notif, sizeof(notif));

#ifdef MSG_DEBUG
  if (++nb_write_messages == PERIODIC_THR_DISPLAY)
  {
    Time now = currentTime();
    float elapsed_in_sec = diffTime(now, write_start_time) / 1000000.0;
    fprintf(stderr, "[%s] write thr: %f msg/sec\n", name, nb_write_messages / elapsed_in_sec);
    nb_write_messages = 0;
    write_start_time = now;
  }
#endif

  return true;
}

Message* Circular_buffer::cb_read_msg()
{
  //fprintf(stderr, "[%s] Reading --- ri: %d, wi: %d \n", name, cb_read_index, cb_write_index);
  if (cb_write_index == cb_read_index)
    return NULL; //the buffer is empty

  //the buffer is not empty
  Message* temp = circular_buffer[cb_read_index];
  //fprintf(stderr, "[%s] cb_read_msg %p at %d\n", name, temp, cb_read_index);
  circular_buffer[cb_read_index] = cb_magic();
  cb_read_index = (cb_read_index + 1) % length;
  read(fd, &notif, sizeof(notif));

#ifdef MSG_DEBUG
  if (++nb_read_messages == PERIODIC_THR_DISPLAY)
  {
    Time now = currentTime();
    float elapsed_in_sec = diffTime(now, read_start_time) / 1000000.0;
    fprintf(stderr, "[%s] read thr: %f msg/sec\n", name, nb_read_messages / elapsed_in_sec);
    nb_read_messages = 0;
    read_start_time = currentTime();
  }
#endif

  return temp;
}

