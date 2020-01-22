4all: libbyz.a

CC:=g++
C:=gcc
AR:=ar
RANLIB:=ranlib
MAKEDEPEND:=makedepend -Y
MAKE=gmake
INCS:=-I../gmp -I../sfs/include/sfslite -iquote . -iquote ../libbyz.common

# -DMSG_DEBUG prints which messages are received by the replica
CFLAGS:= -O0 -g3 -ggdb -Wall -DRECOVERY -march=native -m32 -L/usr/lib32 -DVERIFIER_ONLY #-DMSG_DEBUG
CPPFLAGS:= -std=c++0x $(CFLAGS)

SINCS:= -I../sfslite -I../sfslite/crypt -I../sfslite/async -I../sfslite/arpc -I../sfslite/sfsmisc -I../sfslite/svc -I../sfs/include/sfslite 


Principal.o: ../libbyz.common/Principal.cc
	$(CC) $(CPPFLAGS) $(INCS) $(SINCS) -o $@ -c $<

Node.o: Node.cc
	$(CC) $(CPPFLAGS) $(INCS) $(SINCS) -o $@ -c $<

Verifier_stats.o: Verifier_stats.cc
	$(CC) $(CPPFLAGS) $(INCS) -o $@ -c $< -lm

%.o:: ../libbyz.common/%.cc
	$(CC) $(CPPFLAGS) $(INCS) -o $@ -c $<

%.o:: ../libbyz.common/%.c
	$(C) $(CFLAGS) $(INCS) -o $@ -c $<

%.o:: %.cc
	$(CC) $(CPPFLAGS) $(INCS) -o $@ -c $<

%.o:: %.c
	$(C) $(CFLAGS) $(INCS) -o $@ -c $<


C_FILES=\
../libbyz.common/New_key.cc\
../libbyz.common/Reply.cc \
../libbyz.common/Digest.cc\
../libbyz.common/Status.cc\
../libbyz.common/Prepared_cert.cc\
../libbyz.common/ITimer.cc\
../libbyz.common/Data.cc\
../libbyz.common/Meta_data_cert.cc\
../libbyz.common/View_change_ack.cc \
../libbyz.common/Statistics.cc\
../libbyz.common/Time.cc \
../libbyz.common/attacks.cc\
../libbyz.common/Protocol_instance_change.cc\
../libbyz.common/Node_blacklister.cc\
../libbyz.common/Checkpoint.cc\
../libbyz.common/Commit.cc\
../libbyz.common/Fetch.cc\
../libbyz.common/Message.cc\
../libbyz.common/Meta_data.cc\
../libbyz.common/Meta_data_d.cc\
../libbyz.common/New_view.cc\
../libbyz.common/Prepare.cc\
../libbyz.common/Pre_prepare_info.cc\
../libbyz.common/Req_queue.cc\
../libbyz.common/View_change.cc\
../libbyz.common/Client.cc\
../libbyz.common/tcp_net.cc \
../libbyz.common/Principal.cc \
Rep_info.cc\
Big_req_table.cc\
Verifier.cc\
Verifier_thread.cc \
Node.cc\
Request.cc \
Checkpoint.cc\
Pre_prepare.cc\
State.cc\
libbyz.cc \
View_info.cc\
NV_info.cc\
Wrapped_request.cc  \
Propagate.cc\
Verifier_stats.cc\
Circular_buffer.cc \
Blocking_circular_buffer.cc \
Execution_thread.cc\
Forwarding_thread.cc\


c_FILES= ../libbyz.common/fail.c ../libbyz.common/umac.c

H_FILES := $(C_FILES:%.C=%.H)

h_FILES := $(c_FILES:%.c=%.h)

OBJ_FILES:=$(C_FILES:%.cc=%.o) $(c_FILES:%.c=%.o)

objs: $(OBJ_FILES)

clean:
	-rm ../libbyz.common/*.o
	-rm ../libbyz.common/*~
	-rm *.o
	-rm *~

clobber: clean
	-rm libbyz.a

depend:
	$(MAKEDEPEND) $(INCS) $(SINCS) $(C_FILES)

libbyz.a: objs
	$(AR) rcvs $@ $(OBJ_FILES) 
	$(RANLIB) $@

