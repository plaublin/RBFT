/*
 * tcp_net.h
 *
 * Useful functions to use with the TCP sockets
 *
 *  Created on: 29 oct. 2010
 *      Author: pl
 */

#ifndef TCP_NET_H_
#define TCP_NET_H_

#ifdef __cplusplus
extern "C"
{
#endif

#define NIPQUAD(addr)   ((unsigned char *)&addr)[0], \
                            ((unsigned char *)&addr)[1], \
                        ((unsigned char *)&addr)[2], \
                        ((unsigned char *)&addr)[3]

#define NIPQUADi(addr, i)   ((unsigned char *)&addr)[i]

int recvMsg(int s, void *buf, size_t len);

void sendMsg(int s, void *msg, int size);

void sendMsg_noblock(int s, void *msg, int size);

void print_trace (void);

int get_interface_between_i_and_j(int protocol_instance_id, int i, int j);

#ifdef __cplusplus
}
#endif

#endif /* TCP_NET_H_ */
