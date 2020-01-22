#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Get the pipes throughputs
# Note: this script assumes NB_FAULTS+1 PIRs
#  argv[1]: verifier file


import sys
import os
import re

# Number of tolerated faults
#  -> 3f+1 nodes/replicas
#  -> f+1 replicas per node
NB_FAULTS=2


nb_received_requests = []
nb_sent_requests_to_forwarder = []
nb_recv_requests_from_verifier_thr = []
nb_sent_propagate = []
nb_recv_propagate = []
nb_sent_requests_to_verifier = []
nb_recv_requests_from_forwarder = []
nb_sent_requests_to_replicas = []
nb_recv_requests_from_replicas = []
nb_sent_requests_to_exec = []
nb_recv_requests_from_verifier = []
nb_sent_replies = []


# Initialize the data structures
def init_structures():
   global nb_recv_propagate, nb_recv_requests_from_replicas

   for i in range(0, 3*NB_FAULTS+1):
      nb_recv_propagate.append([])
   for i in range(0, NB_FAULTS+1):
      nb_recv_requests_from_replicas.append([])
# ]-- end init_structures/0


# Load data from the file filename
def load_file(filename):
   global nb_received_requests, nb_sent_requests_to_forwarder, nb_recv_requests_from_verifier_thr, nb_sent_propagate, nb_recv_propagate, nb_sent_requests_to_verifier, nb_recv_requests_from_forwarder, nb_sent_requests_to_replicas, nb_recv_requests_from_replicas, nb_sent_requests_to_exec, nb_recv_requests_from_verifier, nb_sent_replies 

   # list of regex
   regex1 = r"""nb_received_requests=(\d+\.\d+) msg/sec"""; regex1_obj = re.compile(regex1)
   regex2 = r"""nb_sent_requests_to_forwarder=(\d+\.\d+) msg/sec"""; regex2_obj = re.compile(regex2)
   regex3 = r"""nb_recv_requests_from_verifier_thr=(\d+\.\d+) msg/sec"""; regex3_obj = re.compile(regex3)
   regex4 = r"""nb_sent_propagate=(\d+\.\d+) msg/sec"""; regex4_obj = re.compile(regex4)
   regex5 = r"""nb_recv_propagate\[(\d+)\]=(\d+\.\d+) msg/sec"""; regex5_obj = re.compile(regex5)
   regex6 = r"""nb_sent_requests_to_verifier=(\d+\.\d+) msg/sec"""; regex6_obj = re.compile(regex6)
   regex7 = r"""nb_recv_requests_from_forwarder=(\d+\.\d+) msg/sec"""; regex7_obj = re.compile(regex7)
   regex8 = r"""nb_sent_requests_to_replicas=(\d+\.\d+) msg/sec"""; regex8_obj = re.compile(regex8)
   regex9 = r"""nb_recv_requests_from_replicas\[(\d+)\]=(\d+\.\d+) msg/sec"""; regex9_obj = re.compile(regex9)
   regex10 = r"""nb_sent_requests_to_exec=(\d+\.\d+) msg/sec"""; regex10_obj = re.compile(regex10)

   regex11 = r"""nb_recv_requests_from_verifier=(\d+\.\d+) msg/sec"""; regex11_obj = re.compile(regex11)
   regex12 = r"""nb_sent_replies=(\d+\.\d+) msg/sec"""; regex12_obj = re.compile(regex12)

   fd = open(filename, 'r')

   while 1:
      line = fd.readline()
      if not line:
         break

      match_obj = regex1_obj.search(line)
      if match_obj != None:
         nb_received_requests.append(float(match_obj.group(1)))
         
         line = fd.readline()
         match_obj = regex2_obj.search(line)
         if match_obj != None: nb_sent_requests_to_forwarder.append(float(match_obj.group(1)))

         line = fd.readline()
         match_obj = regex3_obj.search(line)
         if match_obj != None: nb_recv_requests_from_verifier_thr.append(float(match_obj.group(1)))
         
         line = fd.readline()
         match_obj = regex4_obj.search(line)
         if match_obj != None: nb_sent_propagate.append(float(match_obj.group(1)))

         for i in xrange(0, 3*NB_FAULTS+1):
            line = fd.readline()
            match_obj = regex5_obj.search(line)
            if match_obj != None: nb_recv_propagate[int(match_obj.group(1))].append(float(match_obj.group(2)))
   
         line = fd.readline()
         match_obj = regex6_obj.search(line)
         if match_obj != None: nb_sent_requests_to_verifier.append(float(match_obj.group(1)))
   
         line = fd.readline()
         match_obj = regex7_obj.search(line)
         if match_obj != None: nb_recv_requests_from_forwarder.append(float(match_obj.group(1)))

         line = fd.readline()
         match_obj = regex8_obj.search(line)
         if match_obj != None: nb_sent_requests_to_replicas.append(float(match_obj.group(1)))

         for i in xrange(0, NB_FAULTS+1):
            line = fd.readline()
            match_obj = regex9_obj.search(line)
            if match_obj != None: nb_recv_requests_from_replicas[int(match_obj.group(1))].append(float(match_obj.group(2)))

         line = fd.readline()
         match_obj = regex10_obj.search(line)
         if match_obj != None: nb_sent_requests_to_exec.append(float(match_obj.group(1)))

         line = fd.readline()
         match_obj = regex11_obj.search(line)
         if match_obj != None: nb_recv_requests_from_verifier.append(float(match_obj.group(1)))
    
         line = fd.readline()
         match_obj = regex12_obj.search(line)
         if match_obj != None: nb_sent_replies.append(float(match_obj.group(1)))
      # ]-- end if
   # ]-- end while

   fd.close()
# ]-- end load_file/1


# Store data to the file filename
def output_values():
   global nb_received_requests, nb_sent_requests_to_forwarder, nb_recv_requests_from_verifier_thr, nb_sent_propagate, nb_recv_propagate, nb_sent_requests_to_verifier, nb_recv_requests_from_forwarder, nb_sent_requests_to_replicas, nb_recv_requests_from_replicas, nb_sent_requests_to_exec, nb_recv_requests_from_verifier, nb_sent_replies 

   print "#i\tnb_received_requests\tnb_sent_requests_to_forwarder\tnb_recv_requests_from_verifier_thr\tnb_sent_propagate",
   for i in xrange(0, 3*NB_FAULTS+1):
      print "\tnb_recv_propagate[" + str(i) + "]",
   print "\tnb_sent_requests_to_verifier\tnb_recv_requests_from_forwarder\tnb_sent_requests_to_replicas",
   for i in xrange(0, NB_FAULTS+1):
      print "\tnb_recv_requests_from_replicas[" + str(i) + "]",
   print "\tnb_sent_requests_to_exec\tnb_recv_requests_from_verifier\tnb_sent_replies"

   for i in xrange(0, len(nb_received_requests)):
      try:
         print str(i),
         print "\t" + str(nb_received_requests[i]),
         print "\t" + str(nb_sent_requests_to_forwarder[i]),
         print "\t" + str(nb_recv_requests_from_verifier_thr[i]),
         print "\t" + str(nb_sent_propagate[i]),
         for j in xrange(0, 3*NB_FAULTS+1):
            print "\t" + str(nb_recv_propagate[j][i]),
         print "\t" + str(nb_sent_requests_to_verifier[i]),
         print "\t" + str(nb_recv_requests_from_forwarder[i]),
         print "\t" + str(nb_sent_requests_to_replicas[i]),
         for j in xrange(0, NB_FAULTS+1):
            print "\t" + str(nb_recv_requests_from_replicas[j][i]),
         print "\t" + str(nb_sent_requests_to_exec[i]),
         print "\t" + str(nb_recv_requests_from_verifier[i]),
         print "\t" + str(nb_sent_replies[i])
      except IndexError:
         break
   # ]-- end for
# ]-- end load_file/0


# ENTRY POINT
if __name__ == "__main__":
   if len(sys.argv) == 2:
      init_structures()
      load_file(sys.argv[1])
      output_values()
   else:
      print("Usage: %s <verifier_file>"%(sys.argv[0])) 

