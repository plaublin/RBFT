#ifndef LAT_REQ_H_
#define LAT_REQ_H_

#include <sys/time.h>

// initialize everything for logging latencies in a file whose
// name is filename
void initialize_lat_req(char *filename);

// add a tuple given a view and a latency (in ms)
void add_lat_req(long long view, float latency);

// add a tuple given a view, the time at which the request has been sent, and the time at which the response has been received
void add_req_times(long long view, struct timeval send_time, struct timeval recv_time);

// finalize the logging of latencies (and close the file if close_file != 0)
void finalize_lat_req(int close_file);

// convert a binary file where latencies are logged to
// a human-readable format
void convert(char *filesrc, char *filedest);

#endif
