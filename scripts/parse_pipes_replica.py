#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Get the pipes throughputs
# Note: this script assumes NB_FAULTS+1 PIRs
#  argv[1]: replica file


import sys
import os
import re

# Number of tolerated faults
#  -> 3f+1 nodes/replicas
#  -> f+1 replicas per node
NB_FAULTS=2


nb_received_requests = []
nb_sent_ordered_requests = []
nb_sent_to_replicas = []
nb_recv_from_replicas = []


# Initialize the data structures
def init_structures():
   global nb_sent_to_replicas, nb_recv_from_replicas

   for i in range(0, 3*NB_FAULTS+1):
      nb_sent_to_replicas.append([])
      nb_recv_from_replicas.append([])
# ]-- end init_structures/0


# Load data from the file filename
def load_file(filename):
   global nb_received_requests, nb_sent_ordered_requests, nb_sent_to_replicas, nb_recv_from_replicas

   # list of regex
   regex1 = r"""nb_received_requests=(\d+\.\d+) msg/sec"""; regex1_obj = re.compile(regex1)
   regex2 = r"""nb_sent_ordered_requests=(\d+\.\d+) msg/sec"""; regex2_obj = re.compile(regex2)
   regex3 = r"""nb_sent_to_replicas\[(\d+)\]=(\d+\.\d+) msg/sec"""; regex3_obj = re.compile(regex3)
   regex4 = r"""nb_recv_from_replicas\[(\d+)\]=(\d+\.\d+) msg/sec"""; regex4_obj = re.compile(regex4)

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
         if match_obj != None: nb_sent_ordered_requests.append(float(match_obj.group(1)))

         for i in xrange(0, 3*NB_FAULTS+1):
            line = fd.readline()
            match_obj = regex3_obj.search(line)
            if match_obj != None: nb_sent_to_replicas[int(match_obj.group(1))].append(float(match_obj.group(2)))

         for i in xrange(0, 3*NB_FAULTS+1):
            line = fd.readline()
            match_obj = regex4_obj.search(line)
            if match_obj != None: nb_recv_from_replicas[int(match_obj.group(1))].append(float(match_obj.group(2)))
      # ]-- end if
   # ]-- end while

   fd.close()
# ]-- end load_file/1


# Store data to the file filename
def output_values():
   global nb_received_requests, nb_sent_ordered_requests, nb_sent_to_replicas, nb_recv_from_replicas

   print "#i\tnb_received_requests\tnb_sent_ordered_requests",
   for i in xrange(0, 3*NB_FAULTS+1):
      print "\tnb_sent_to_replicas[" + str(i) + "]",
   for i in xrange(0, 3*NB_FAULTS+1):
      print "\tnb_recv_from_replicas[" + str(i) + "]",

   for i in xrange(0, len(nb_received_requests)):
      try:
         print str(i),
         print "\t" + str(nb_received_requests[i]),
         print "\t" + str(nb_sent_ordered_requests[i]),
         for j in xrange(0, 3*NB_FAULTS+1):
            print "\t" + str(nb_sent_to_replicas[j][i]),
         for j in xrange(0, 3*NB_FAULTS+1):
            print "\t" + str(nb_recv_from_replicas[j][i]),
         print
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
      print("Usage: %s <replica_file>"%(sys.argv[0])) 

