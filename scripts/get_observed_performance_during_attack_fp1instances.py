#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Get, for each node, the performance observed for each PIR
# Note: this script assumes 2 PIRs and f=1
#  argv[1]: experiment directory


import sys
import os
import re
import scipy

num_faults = 1

def load_results(directory):
   print("#\tPIR_X\tlat +/- stdev (ms)\tthr +/- stdev (req/s)")

   thr_min_ratio = float("inf")
   thr_max_ratio = 0

   file_list = [x for x in os.listdir(directory) if x.startswith('verifier_') and x.endswith('.log')]
   for f in file_list:
      R = load_file(directory + '/' + f)
      (lat, thr) = get_avg_stdev(R)
      m = get_min_max_thr_ratio(R)
      if m[0] < thr_min_ratio:
         thr_min_ratio = m[0]
      if m[1] > thr_max_ratio:
         thr_max_ratio = m[1]

      lr = compute_ratio(lat)
      tr = compute_ratio(thr)
      
      print("%s"%(f))
      for p in (0, 1):
         try:
            print("\tPIR%d\t%.2f +/- %.2f\t%d +/- %d"%(p, lat[p][0], lat[p][1], thr[p][0], thr[p][1]))
         except TypeError:
            print("\tPIR%d\t0.0 +/- 0.0\t0 +/- 0"%(p))
      print("\tratio: lat = %.2f\tthr = %.2f"%(lr, tr))

   print("\nThr min ratio is %.4f"%(thr_min_ratio))
   print("Thr max ratio is %.4f"%(thr_max_ratio))
# ]-- end load_results/1


# load the results from the file
# input: verifier log file
# output: dictionnary { snapshot_id -> (time, (lat_PIR0, lat_PIR1),
#                                      (thr_PIR0, thr_PIR1),
#                                      (nbr_PIR0, nbr_PIR1)) }
def load_file(filename):
   monitoring_stats = {}
   monitoring_period = 0
   statTimer_time_first_call = 0
   sid = -1
  
   fd = open(filename, 'r')

   for line in fd:
      # if starts with statTimer_time_first_call then save the time and the monitoring period
      if line.startswith("statTimer_time_first_call"):
         zeline = line.split()
         statTimer_time_first_call = int(zeline[1])
         monitoring_period = int(zeline[4])
         continue

      # if len == 12 then this is a statistic. Beware of the ValueError exception
      zeline = line.split()
      if len(zeline) == 9:
         # snapshot_id    #lat_PIR_0      #lat_PIR_1      #lat_ratio
            #thr_PIR_0      #thr_PIR_1      #thr_ratio
               #nb_reqs_PIR_0      #nb_reqs_PIR_1
         try:
            sid = int(zeline[0])
            lat_pir0 = float(zeline[1])
            lat_pir1 = float(zeline[2])
            lat_ratio = float(zeline[3])
            thr_pir0 = float(zeline[4])
            thr_pir1 = float(zeline[5])
            thr_ratio = float(zeline[6])
            nbr_pir0 = float(zeline[7])
            nbr_pir1 = float(zeline[8])

            # all the times are in usec and relative to the beginning
            # of the execution of the verifier
            time = statTimer_time_first_call + monitoring_period * sid

            lat_tuple = (lat_pir0, lat_pir1)
            thr_tuple = (thr_pir0, thr_pir1)
            nbr_tuple = (nbr_pir0, nbr_pir1)
            monitoring_stats[sid] = (time, lat_tuple, thr_tuple, nbr_tuple)
         except ValueError:
            pass
      # ]-- end if
   # ]-- end for

   fd.close()

   # delete the tuple for the last snapshot id: the results may not be valid because
   # this is the end of the experiment
   if sid > -1:
      del monitoring_stats[sid]

   return monitoring_stats
# ]-- end load_file/1


# get the min and the max ratio over all the snapshots
# input: dictionnary { snapshot_id -> (time, (lat_PIR0, lat_PIR1),
#                                      (thr_PIR0, thr_PIR1),
#                                      (nbr_PIR0, nbr_PIR1)) }
# output: tuple (min ratio, max_ratio) (float values)
def get_min_max_thr_ratio(stats):
   min_ratio = float("inf")
   max_ratio = 0

   for k in stats:
      T = stats[k]
      r = compute_ratio( ((T[2][0], 0), (T[2][1], 0)) )
      if r < min_ratio:
         min_ratio = r
      if r > max_ratio:
         max_ratio = r

   return (min_ratio, max_ratio)
# ]-- end get_min_thr_ratio/1


# Compute the ratio, given the tuple T
# Format of T: ( (avg_pir0, stdev_pir0), (avg_pir1, stdev_pir1) )
def compute_ratio(T):
   master = T[0][0]
   backup = T[1][0]
   return (master - backup)/master
# ]-- end compute_ratio/1


# input: dictionnary resulting from a call to load_file/1
# output: tuple ( lat_avg_stdev, thr_avg_stdev )
#         where XXX_avg_stdev = ( (avg_pir0, stdev_pir0), (avg_pir1, stdev_pir1) )
def get_avg_stdev(stats):
   lat_pirs = ([], [])
   thr_pirs = ([], [])

   for k in stats:
      T = stats[k]
      for i in (0, 1):
         lat_pirs[i].append(T[1][i])
         thr_pirs[i].append(T[2][i])
   # ]-- end for

   # lat_avg_stdev[i] = lat (avg, stdev) of PIR i
   lat_avg_stdev = []

   for A in lat_pirs:
      # get avg and stdev
      A = scipy.array(A)
      a = A.mean()
      s = A.std()
      lat_avg_stdev.append((a, s))

   # thr_avg_stdev[i] = thr (avg, stdev) of PIR i
   thr_avg_stdev = []

   for A in thr_pirs:
      # get avg and stdev
      A = scipy.array(A)
      a = A.mean()
      s = A.std()
      thr_avg_stdev.append((a, s))

   return (lat_avg_stdev, thr_avg_stdev)
# ]-- end get_avg_stdev/1


# ENTRY POINT
if __name__ == "__main__":
   if len(sys.argv) == 2:
      load_results(sys.argv[1])
   else:
      print("Usage: %s <experiment_directory>"%(sys.argv[0])) 

