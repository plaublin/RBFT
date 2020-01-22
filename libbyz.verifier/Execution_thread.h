/*
 * execution_thread.h
 *
 *  Created on: 3 mai 2011
 *      Author: benmokhtar
 */

#ifndef EXECUTION_THREAD_H_
#define EXECUTION_THREAD_H_

#include "types.h"

#include <map>

class Wrapped_request;
class Request;
class Reply;

class Execution_thread
{

public:
  void *run(void);

private:
  void handle(Wrapped_request *m);
  void handle(Request *m);

  Request* get_request_from_certificate(int cid, Request_id rid);

  void delete_old_certificates(int cid, Request_id rid);

};
extern "C" void *Execution_thread_startup(void *);

#endif /* EXECUTION_THREAD_H_ */
