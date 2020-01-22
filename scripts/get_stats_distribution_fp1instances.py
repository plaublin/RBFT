#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Get the distribution of the monitoring stats
#  argv[1]: experiment directory
#  argv[2]: lat output file
#  argv[3]: thr output file
#  argv[4]: mode: if true, then get the values for all nodes in order to compute the
#           distribution; if false, then get the values for the 2f+1 nodes that observe
#           the greatest difference, as they are the one which are likely to send a
#           protocol instance change
#
# Note: this script assumes f+1 protocol instances


import math
import sys
import os
import re


NUM_FAULTS=1
BUCKET_LENGTH = 0.01

# number of PROPAGATE messages needed to initiate a protocol instance change
def threshold_for_protocol_instance_change():
   return 2*NUM_FAULTS+1
# ]-- end threshold_for_protocol_instance_change/0


# load the results for all the stats files
def load_files(directory):
   # dictionnary { node -> ([lat_ratios], [thr_ratios])}
   ratios = {}

   file_list = [x for x in os.listdir(directory) if x.startswith('verifier_sci') and x.endswith('.log')]
   for f in file_list:
      ratios[f] = load_file(directory + '/' + f)

   return ratios
# ]-- end load_files/1


# load the results from one stats file
def load_file(filename):
   T = ([], [])
   fd = open(filename, 'r')

   for line in fd:
      # do not read comments
      if line.startswith("#"):
         continue

      # if len == 12 then this is a statistic. Beware of the ValueError exception
      zeline = line.split()
      if len(zeline) == 9:
         # snapshot_id    #lat_PIR_0      #lat_PIR_1      #lat_ratio
            #thr_PIR_0      #thr_PIR_1      #thr_ratio
               #nb_reqs_PIR_0      #nb_reqs_PIR_1
         try:
            #sid = int(zeline[0])
            #lat_pir0 = float(zeline[1])
            #lat_pir1 = float(zeline[2])
            lat_ratio = float(zeline[3])
            #thr_pir0 = float(zeline[4])
            #thr_pir1 = float(zeline[5])
            thr_ratio = float(zeline[6])
            #nbr_pir0 = float(zeline[7])
            #nbr_pir1 = float(zeline[8])

            T[0].append(lat_ratio)
            T[1].append(thr_ratio)

         except ValueError:
            pass
      # ]-- end if
   # ]-- end for

   fd.close()
   return T
# ]-- end load_file/1


def get_list_of_ratios(ratios, mode):
   LAT = []
   THR = []

   if mode == "True" or mode == "true":
      for t in ratios:
         LAT.extend(ratios[t][0])
         THR.extend(ratios[t][1])

   else:
      s = float('inf')
      for t in ratios:
         s = min(s, len(ratios[t]))

      for i in xrange(s):
         values = ([], [])
         for t in ratios:
            values[0].append(ratios[t][0][i])
            values[1].append(ratios[t][1][i])
         # now that I have the 3f+1 values, get the most interesting ones
         new_lat = sorted(values[0])[len(values[0])-threshold_for_protocol_instance_change():]
         new_thr = sorted(values[1])[:threshold_for_protocol_instance_change()]
         LAT.extend(new_lat)
         THR.extend(new_thr)

   return (sorted(LAT), sorted(THR))
# ]-- end get_list_of_ratios/2


def compute_distribution(L, outfile, name):
   fd = open(outfile, "w")

   fd.write("# ratio\t%values\t%values_cumul\n")

   s = len(L)
   maximum = max(L)
   i = 0
   n = 0
   c = 0

   minimum = L[0]
   while minimum == -float('inf') and i < s:
      n += 1
      i += 1
      minimum = L[i]
   
   if n > 0:
      p = n * 100.0 / s
      c += p
      n = 0
   upper = minimum + BUCKET_LENGTH

   
   while i < s:
      if L[i] >= upper:
         p = n * 100.0 / s
         c += p

         fd.write("%.3f\t%.3f\t%.3f\n"%(upper, p, c))

         n = 0
         upper += BUCKET_LENGTH
      else:
         n += 1
         i += 1
   # ]-- end while

   if n > 0:
      p = n * 100.0 / s
      c += p

      fd.write("%.3f\t%.3f\t%.3f\n"%(upper, p, c))
   # ]-- end if

   fd.close()
# ]-- end compute_distribution/3


# ENTRY POINT
if __name__ == "__main__":
   if len(sys.argv) == 5:
      ratios = load_files(sys.argv[1])
      (L_LAT, L_THR) = get_list_of_ratios(ratios, sys.argv[4])
      compute_distribution(L_LAT, sys.argv[2], "latency")
      compute_distribution(L_THR, sys.argv[3], "throughput")
   else:
      print("Usage: %s <experiment_directory> <lat_output_file> <thr_output_file> <get_values_for_all_nodes:true or false>"%(sys.argv[0])) 
