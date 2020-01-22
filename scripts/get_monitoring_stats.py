#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Get the monitoring stats
# Note: this script assumes 3 PIRs
#  argv[1]: verifier file
#  argv[2]: output file


import sys
import os
import re


# all the times are in usec and relative to the beginning

# dictionnary { snapshot_id -> (time, (lat_PIR0, lat_PIR1, lat_PIR2, ratio),
#                                     (thr_PIR0, thr_PIR1, thr_PIR2, ratio)) }
monitoring_stats = {}

# list of protocol instance change times
protocol_instance_changes = []


##########################################################
##### UTILITY FUNCTIONS #####

# given a snapshot_id, get the time
def get_time_of_snapshot_id(sid):
   return monitoring_stats[sid][0]
# ]-- end get_time_of_snapshot_id/1


# given a snapshot_id, get the lat tuple
def get_lat_tuple_of_snapshot_id(sid):
   return monitoring_stats[sid][1]
# ]-- end get_lat_tuple_of_snapshot_id/1


# given a snapshot_id, get the time
def get_thr_tuple_of_snapshot_id(sid):
   return monitoring_stats[sid][2]
# ]-- end get_thr_tuple_of_snapshot_id/1


# return the average of the values in the tuple T
def average(T):
   return sum(T) / len(T)
# ]-- end average/1
##########################################################


# load the results from the file
def load_file(filename):
   global monitoring_stats

   monitoring_period = 0
   statTimer_time_first_call = 0
   sid = -1
  
   fd = open(filename, 'r')

   for line in fd:
      # if starts with PROTOCOL_INSTANCE_CHANGE then log it
      if line.startswith("PROTOCOL_INSTANCE_CHANGE"):
         protocol_instance_changes.append(int(line.split()[2]))
         continue

      # if starts with statTimer_time_first_call then save the time and the monitoring period
      if line.startswith("statTimer_time_first_call"):
         zeline = line.split()
         statTimer_time_first_call = int(zeline[1])
         monitoring_period = int(zeline[4])
         continue

      # if len == 9 then this is a statistic. Beware of the ValueError exception
      zeline = line.split()
      if len(zeline) == 12:
         # snapshot_id    #lat_PIR_0      #lat_PIR_1      #lat_PIR_2      #lat_ratio
            #thr_PIR_0      #thr_PIR_1      #thr_PIR_2      #thr_ratio
               #nb_reqs_PIR_0      #nb_reqs_PIR_1      #nb_reqs_PIR_2
         try:
            sid = int(zeline[0])
            lat_pir0 = float(zeline[1])
            lat_pir1 = float(zeline[2])
            lat_pir2 = float(zeline[3])
            lat_ratio = float(zeline[4])
            thr_pir0 = float(zeline[5])
            thr_pir1 = float(zeline[6])
            thr_pir2 = float(zeline[7])
            thr_ratio = float(zeline[8])

            time = statTimer_time_first_call + monitoring_period * sid

            # we compare the performance of the master instance with the performance
            # of the f best backup instances

            # ratio
            #lat_ratio = lat_pir0 / average((lat_pir1, lat_pir2))
            #thr_ratio = thr_pir0 / average((thr_pir1, thr_pir2))

            # difference
            #lat_ratio = lat_pir0 - average((lat_pir1, lat_pir2))
            #thr_ratio = thr_pir0 - average((thr_pir1, thr_pir2))
            
            # absolute difference in %tage
            #lat_ratio = abs((lat_pir0 - average((lat_pir1, lat_pir2)))*100/lat_pir0)
            #thr_ratio = abs((thr_pir0 - average((thr_pir1, thr_pir2)))*100/thr_pir0)
            
            # difference in %tage
            #lat_ratio = (lat_pir0 - min((lat_pir1, lat_pir2)))*100/lat_pir0
            #thr_ratio = (thr_pir0 - max((thr_pir1, thr_pir2)))*100/thr_pir0

            lat_ratio = lat_ratio * 100.0
            thr_ratio = thr_ratio * 100.0

            lat_tuple = (lat_pir0, lat_pir1, lat_pir2, lat_ratio)
            thr_tuple = (thr_pir0, thr_pir1, thr_pir2, thr_ratio)
            monitoring_stats[sid] = (time, lat_tuple, thr_tuple)
         except ValueError:
            pass
      # ]-- end if
   # ]-- end for

   if sid > -1:
      del monitoring_stats[sid]

   fd.close()
# ]-- end load_file/1


# prints the PIC times and the code to add to your gnuplot script
def get_pic_times():
   #print("set style line 4 linecolor rgb \"blue\"")

   for t in protocol_instance_changes:
      time_sec = t / 1000000.0

      lat_ratio = 0
      thr_ratio = 0
      for i in sorted(monitoring_stats):
         if get_time_of_snapshot_id(i) > t:
            break

         lat_ratio = get_lat_tuple_of_snapshot_id(i)[3]
         thr_ratio = get_thr_tuple_of_snapshot_id(i)[3]
      # ]-- end for

      print("# PIC at %f sec: lat=%f, thr=%f"%(time_sec, lat_ratio, thr_ratio))
      print("set arrow from %f, %f to %f, %f nohead ls 4"%(time_sec, 0, time_sec, 1.5))
   # ]-- end for
# ]-- end get_pic_times/0


# prints the results in outfile
def output_values(outfile):
   fd = open(outfile, 'w')

   fd.write("#snapshot_id\ttime_sec\tratio_lat\tratio_thr\n")

   for i in sorted(monitoring_stats):
      fd.write("%d\t%f\t%f\t%f\n"%(i, get_time_of_snapshot_id(i)/1000000.0, get_lat_tuple_of_snapshot_id(i)[3], get_thr_tuple_of_snapshot_id(i)[3]))

   fd.close()
# ]-- end output_values/1


# ENTRY POINT
if __name__ == "__main__":
    if len(sys.argv) == 3:
        load_file(sys.argv[1])
        output_values(sys.argv[2])
        get_pic_times()
    else:
        print("Usage: %s <verifier_file> <output_file>"%(sys.argv[0])) 
