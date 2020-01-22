/*
 * tcp_net.c
 *
 *  Created on: 29 oct. 2010
 *      Author: pl
 */

#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <errno.h>
#include <execinfo.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "ITimer.h"
#include "tcp_net.h"
#if defined(PIR_ONLY)
#include "PIR.h"
#elif defined(VERIFIER_ONLY)
#include "Verifier.h"
#endif
 
int recvMsg(int s, void *buf, size_t len)
{
  size_t len_tmp = 0;
  int n;

  do
  {
    n = recv(s, &(((char *) buf)[len_tmp]), len - len_tmp, 0);
    if (n == -1)
    {
      kill(getpid(), SIGINT);
      fprintf(stderr, "Error while receiving on fd %d\n", s);
      perror("tcp_net:recv():");
      print_trace();
//      exit(-1);
sleep(3600);
    } else if (n == 0) {
      kill(getpid(), SIGINT);
      fprintf(stderr, "Error while receiving on fd %d\n", s);
      print_trace();
//      exit(-1);
sleep(3600);
    }
    len_tmp = len_tmp + n;
  } while (len_tmp < len);

  return len_tmp;
}

void sendMsg(int s, void *msg, int size)
{
  int total = 0; // how many bytes we've sent
  int bytesleft = size; // how many we have left to send
  int n = -1;

  while (total < size)
  {
    n = send(s, (char*) msg + total, bytesleft, 0);
    if (n == -1)
    {
      kill(getpid(), SIGINT);
      fprintf(stderr, "Error while receiving on fd %d\n", s);
      perror("tcp_net:send():");
      print_trace();
//      exit(-1);
sleep(3600);
    }
    total += n;
    bytesleft -= n;
  }
}

void sendMsg_noblock(int s, void *msg, int size)
{
  int total = 0; // how many bytes we've sent
  int bytesleft = size; // how many we have left to send
  int n = -1;

  while (total < size)
  {
    n = send(s, (char*) msg + total, bytesleft, MSG_DONTWAIT);
    if (n == -1)
    {
      int errsv = errno;
      if (errsv == EAGAIN || errsv == EWOULDBLOCK)
      {
        break;
      }
      else
      {
        kill(getpid(), SIGINT);
        fprintf(stderr, "Error while sending on fd %d\n", s);
        perror("tcp_net:send():");
        print_trace();
//        exit(-1);
sleep(3600);
      }
    }
    total += n;
    bytesleft -= n;
  }
}

/* Obtain a backtrace and print it to stdout. */
void print_trace (void)   {
  void *array[10];
  size_t size;
  char **strings;
  size_t i;
  size = backtrace (array, 10);
  strings = backtrace_symbols (array, size);
  fprintf(stderr, "Obtained %zd stack frames.\n", size);
  for (i = 0; i < size; i++)
     fprintf(stderr, "%s\n", strings[i]);
  free(strings);
}

int get_interface_between_i_and_j(int protocol_instance_id, int i, int j) {
  int interface = 255;

  switch (protocol_instance_id) {
    case 0:
      if (i == 0) {
        if (j == 0) {
          interface = 21;
        } else if (j == 1) {
          interface = 22;
        } else if (j == 2) {
          interface = 23;
        } else if (j == 3) {
          interface = 24;
        }
      } else if (i == 1) {
        if (j == 0) {
          interface = 22;
        } else if (j == 1) {
          interface = 21;
        } else if (j == 2) {
          interface = 24;
        } else if (j == 3) {
          interface = 23;
        }
      } else if (i == 2) {
        if (j == 0) {
          interface = 23;
        } else if (j == 1) {
          interface = 24;
        } else if (j == 2) {
          interface = 21;
        } else if (j == 3) {
          interface = 22;
        }
      } else if (i == 3) {
        if (j == 0) {
          interface = 24;
        } else if (j == 1) {
          interface = 23;
        } else if (j == 2) {
          interface = 22;
        } else if (j == 3) {
          interface = 21;
        }
      }
      break;

    case 1:
      if (i == 0) {
        if (j == 0) {
          interface = 21;
        } else if (j == 1) {
          interface = 26;
        } else if (j == 2) {
          interface = 28;
        } else if (j == 3) {
          interface = 27;
        }
      } else if (i == 1) {
        if (j == 0) {
          interface = 26;
        } else if (j == 1) {
          interface = 21;
        } else if (j == 2) {
          interface = 27;
        } else if (j == 3) {
          interface = 28;
        }
      } else if (i == 2) {
        if (j == 0) {
          interface = 28;
        } else if (j == 1) {
          interface = 27;
        } else if (j == 2) {
          interface = 21;
        } else if (j == 3) {
          interface = 26;
        }
      } else if (i == 3) {
        if (j == 0) {
          interface = 27;
        } else if (j == 1) {
          interface = 28;
        } else if (j == 2) {
          interface = 26;
        } else if (j == 3) {
          interface = 21;
        }
      }
      break;

    default:
          fprintf(stderr, "get_interface_between_i_and_j(%d, %d, %d): unknown protocol instance %d\n", protocol_instance_id, i, j, protocol_instance_id);
  }

  return interface;
}

