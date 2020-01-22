#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "lat_req.h"

// size of a memory page. Static value
#define PAGE_SIZE 4096

// structure used to store the view, the send time and the recv time associated with 1 request (and its response)
typedef struct latency_requests
{
  long long view;
  struct timeval sndtime;
  struct timeval rcvtime;
} latency_requests;

// the file descriptor where to output the values
static int fd;

// the structure which is written to the file
static struct latency_requests *lat_req;

// the size of the structure
static int lat_req_size;

// current index in the structure
static int lat_req_idx;

// initialize everything for logging latencies in a file whose
// name is filename
void initialize_lat_req(char *filename)
{
  fd = open(filename, O_CREAT | O_WRONLY, 0640);

  lat_req_size = PAGE_SIZE / sizeof(struct latency_requests);
  lat_req = (struct latency_requests*) malloc(
      sizeof(struct latency_requests) * lat_req_size);
  lat_req_idx = 0;
}

void finalize_add(void)
{
  lat_req_idx++;
  if (lat_req_idx == lat_req_size)
  {
    write(fd, lat_req, sizeof(struct latency_requests) * lat_req_size);
    lat_req_idx = 0;
  }
}

// add a tuple given a view and a latency (in ms)
void add_lat_req(long long view, float latency)
{
  lat_req[lat_req_idx].view = view;
  lat_req[lat_req_idx].sndtime.tv_sec = 0;
  lat_req[lat_req_idx].sndtime.tv_usec = 0;
  lat_req[lat_req_idx].rcvtime.tv_sec = 0;
  lat_req[lat_req_idx].rcvtime.tv_usec = (suseconds_t) (latency * 1000.0);

  finalize_add();
}

// add a tuple given a view, the time at which the request has been sent, and the time at which the response has been received
void add_req_times(long long view, struct timeval send_time, struct timeval recv_time)
{
  lat_req[lat_req_idx].view = view;
  lat_req[lat_req_idx].sndtime = send_time;
  lat_req[lat_req_idx].rcvtime = recv_time;

  finalize_add();
}

// finalize the logging of latencies (and close the file)
void finalize_lat_req(int close_file)
{
  // fill lat_req since its size is not a multiple of the bursts size
  //fprintf(stderr, "lat_req_idx=%i\n", lat_req_idx);

  while (lat_req_idx < lat_req_size)
  {
     lat_req[lat_req_idx].view = 0;
     lat_req[lat_req_idx].sndtime.tv_sec = 0;
     lat_req[lat_req_idx].sndtime.tv_usec = 0;
     lat_req[lat_req_idx].rcvtime.tv_sec = 0;
     lat_req[lat_req_idx].rcvtime.tv_usec = 0;
     lat_req_idx++;
  }
  lat_req_idx = 0;

  write(fd, lat_req, sizeof(struct latency_requests) * lat_req_size);

  if (close_file)
  {
    close(fd);
  }
}

// convert a binary file where latencies are logged to
// a human-readable format
void convert(char *filesrc, char *filedest)
{
  FILE* F;
  int fd;
  struct latency_requests lat_req;

  fd = open(filesrc, O_RDONLY);

  if ((F = fopen(filedest, "w")) != NULL)
  {
    fprintf(F, "#view\tsnd_time.sec\tsnd_time.usec\trcv_time.sec\trcv_time.usec\n");
  }

  int reed = 1;
  while (reed != 0)
  {
    reed = read(fd, (void*) &lat_req, sizeof(lat_req));

    if (/*lat_req.view != 0 &&*/ lat_req.sndtime.tv_sec != 0 && lat_req.sndtime.tv_usec != 0 && lat_req.rcvtime.tv_sec != 0 && lat_req.rcvtime.tv_usec != 0)
    {
      fprintf(F, "%qd\t%qu\t%qu\t%qu\t%qu\n", lat_req.view, (unsigned long long)lat_req.sndtime.tv_sec, (unsigned long long)lat_req.sndtime.tv_usec, (unsigned long long)lat_req.rcvtime.tv_sec, (unsigned long long)lat_req.rcvtime.tv_usec);
    }
  }

  close(fd);
  fclose(F);
}
