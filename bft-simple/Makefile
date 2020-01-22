all: bft_manager bft_client bft_pir bft_verifier bft_latency_extractor client_trace_fluctuating_load \
	 client_dynamic_workload manager_dynamic_workload client_openloop client_dynamic_workload_openloop

CC:=g++
AR:=ar
RANLIB:=ranlib
MAKEDEPEND:=makedepend -Y
LIBBYZ_VERIFIER=../libbyz.verifier/libbyz.a
LIBBYZ_PIR=../libbyz.pir/libbyz.a
INCS:=-I../gmp -I../sfs/include/sfslite -I../libbyz.common
INCS_VERIFIER:=$(INCS) -I../libbyz.verifier
INCS_PIR:=$(INCS) -I../libbyz.pir

# info about flags:
#  DEBUG: print debug info
#CPPFLAGS:= $(INCS) -Wall -ansi #-O2 -malign-double -funroll-loops -march=pentiumpro -fomit-frame-pointer -fno-exceptions -ffast-math 
#CPPFLAGS :=-g3 -ggdb -O2 -march=pentium4 -fno-exceptions -DRECOVERY -DDEBUG
CPPFLAGS:= $(INCS) -std=c++0x -O0 -g3 -ggdb -Wall -DRECOVERY -march=native -m32 -L/usr/lib32 #-DMSG_DEBUG
CPPFLAGS_VERIFIER := $(INCS_VERIFIER) $(CPPFLAGS)
CPPFLAGS_PIR := $(INCS_PIR) $(CPPFLAGS)

C:=gcc
CFLAGS_VERIFIER:= $(CPPFLAGS_VERIFIER)
CFLAGS_PIR:= $(CPPFLAGS_PIR)
LIBDIRS:=-L../gmp -L../sfs/lib/sfslite
LIBDIRS_VERIFIER:=$(LIBDIRS) -L../libbyz.verifier -lpthread
LIBDIRS_PIR:=$(LIBDIRS) -L../libbyz.pir
LIBS:=-lbyz -lsfscrypt -lasync -lgmp -larpc -lresolv

MAKE=gmake

C_FILES=\
client.cc verifier.cc pir.cc manager.cc manager2.cc
c_FILES=\

H_FILES := $(C_FILES:%.C=%.H)

h_FILES := $(c_FILES:%.c=%.h)

OBJ_FILES:=$(C_FILES:%.cc=%.o) $(c_FILES:%.c=%.o)
	
objs: $(OBJ_FILES)

clean:
	-rm -f *.o
	-rm -f *~

clobber: clean
	-rm -f bft_manager bft_client bft_pir bft_verifier bft_latency_extractor bft_test_runner_bft_simple \
	       client_trace_fluctuating_load client_dynamic_workload manager_dynamic_workload client_openloop \
	       client_dynamic_workload_openloop

depend:
	$(MAKEDEPEND) $(INCS) $(SINCS) $(C_FILES)


# client
bft_client: lat_req.c client.cc
	$(CC) -o $@ -DCLIENT_MAIN=main $(CPPFLAGS_VERIFIER) $(LIBDIRS_VERIFIER) $^ $(LIBS) -lm
	
# client open loop
client_openloop: lat_req.c client_openloop.cc
	$(CC) -o $@ -DCLIENT_MAIN=main $(CPPFLAGS_VERIFIER) $(LIBDIRS_VERIFIER) $^ $(LIBS) -lm -lpthread
	
# client_trace_fluctuating_load
client_trace_fluctuating_load: lat_req.c client_trace_fluctuating_load.cc
	$(CC) -o $@ -DCLIENT_MAIN=main $(CPPFLAGS_VERIFIER) $(LIBDIRS_VERIFIER) $^ $(LIBS) -lm
	
# client_dynamic_workload
client_dynamic_workload: lat_req.c client_dynamic_workload.cc
	$(CC) -o $@ $(CPPFLAGS_VERIFIER) $(LIBDIRS_VERIFIER) $^ $(LIBS) -lm
	
# client_dynamic_workload_openloop
client_dynamic_workload_openloop: lat_req.c client_dynamic_workload_openloop.cc
	$(CC) -o $@ $(CPPFLAGS_VERIFIER) $(LIBDIRS_VERIFIER) $^ $(LIBS) -lm -lpthread

# Verifier
bft_verifier:  verifier.cc
	$(CC) -o $@ -DVERIFIER_MAIN=main $(CPPFLAGS_VERIFIER) $(LIBDIRS_VERIFIER) $^ $(LIBS)
	
# Protocol Instance Replica (PIR)
bft_pir:  pir.cc
	$(CC) -o $@ -DPIR_MAIN=main $(CPPFLAGS_PIR) $(LIBDIRS_PIR) $^ $(LIBS)

# manager
manager2: stats_utils.cc manager2.cc
	$(CC) -o $@ -DMANAGER_MAIN=main $(CPPFLAGS_VERIFIER) $(LIBDIRS_VERIFIER) $^ $(LIBS) -lm

bft_manager: stats_utils.cc manager.cc
	$(CC) -o $@ -DMANAGER_MAIN=main $(CPPFLAGS_VERIFIER) $(LIBDIRS_VERIFIER) $^ $(LIBS) -lm

manager_dynamic_workload: stats_utils.cc manager_dynamic_workload.cc
	$(CC) -o $@ $(CPPFLAGS_VERIFIER) $(LIBDIRS_VERIFIER) $^ $(LIBS) -lm

# latency extractor
bft_latency_extractor: lat_req.c latency_extractor.cc
	$(CC) -o $@ -DLATENCY_EXTRACTOR_MAIN=main $(CPPFLAGS) $^

#bft_test_runner_bft_simple: test* stats_utils.cc
#	$(CC) -o $@ -DTEST_RUNNER_BFT_SIMPLE_MAIN=main $(CPPFLAGS_VERIFIER) $(LIBDIRS_VERIFIER) $^ $(LIBS) -lm -lcppunit

