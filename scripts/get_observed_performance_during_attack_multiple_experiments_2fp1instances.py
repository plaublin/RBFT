#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Get, for each node, the performance observed for each PIR
# Works with multiple experiments.
# get_observed_performance_during_attack.sh has already been called on each directory
# Note: this script assumes 3 PIRs and f=1
#  args: experiment directories


import sys
import os
import re
import scipy

num_faults = 1

# dictionnary {"node_name" : [list of Node_performance, ...]}
performance_per_node = {}

# Performance of a node = for each PIR, observed latency and throughput (average + stdev)
class Node_performance:
   #latency: array of (lat, stdev) for each PIR
   #throughput: array of (thr, stdev) for each PIR
   #name: string identifying the node

   def __init__(self, name):
      self.latency = []
      self.throughput = []
      self.name = name
   # ]-- end __init__/1

   def add_pir_latency(self, avg, stdev):
      self.latency.append((avg, stdev))
   # ]-- end add_pir_latency/3

   def add_pir_throughput(self, avg, stdev):
      self.throughput.append((avg, stdev))
   # ]-- end add_pir_throughput/3

   def __str__(self):
      str = self.name + "\n"
      for p in xrange(0, len(min(self.latency, self.throughput))):
         str = str + "\tPIR%d\t%.2f +/- %.2f\t%d +/- %d\n"%(p, self.latency[p][0],
            self.latency[p][1], self.throughput[p][0], self.throughput[p][1])
      return str
   # ]-- end __str__/1
# ]-- end class Node_performance


def add_node_performance(N):
   global performance_per_node

   A = performance_per_node.get(N.name)
   if A == None:
      performance_per_node[N.name] = [N]
   else:
      performance_per_node[N.name].append(N)
# ]-- end add_node_performance/1


def load_results(directory):
   fd = open(directory + "/observed_performance.dat", 'r')
   
   N = None
   for line in fd:
      if line.startswith("#"):
         continue

      if line.startswith("verifier"):
         if N != None:
            add_node_performance(N)
         N = Node_performance(line.strip())

      # Note: we assume that the PIR lines are written in order (i.e. from PIR0 to PIR2)
      elif "PIR" in line and N != None:
         zeline = line.split()
         if len(zeline) != 7: continue

         lat_avg = float(zeline[1])
         lat_std = float(zeline[3])
         thr_avg = float(zeline[4])
         thr_std = float(zeline[6])

         N.add_pir_latency(lat_avg, lat_std)
         N.add_pir_throughput(thr_avg, thr_std)
      # ]-- end if
   # ]-- end for

   # the last one
   if N != None:
      add_node_performance(N)

   fd.close()
# ]-- end load_results/1


def print_results_for_bargraph(nb_exp):
   global performance_per_node

   header="#Node_name"
   for p in range(0, 3):
      for i in xrange(nb_exp):
         header = header + "\tPIR%d_%d"%(p, i)

   # latency, average
   print("#Latency, average\n" + header)
   for node_name in performance_per_node:
      print node_name,
      for p in range(0, 3):
         for node_perf in performance_per_node[node_name]:
            print "\t" + str(node_perf.latency[p][0]),
      print
   print

   # latency, stdev
   print("#Latency, stdev\n" + header)
   for node_name in performance_per_node:
      print node_name,
      for p in range(0, 3):
         for node_perf in performance_per_node[node_name]:
            print "\t" + str(node_perf.latency[p][1]),
      print
   print


   # throughput, average
   print("#Throughput, average\n" + header)
   for node_name in performance_per_node:
      print node_name,
      for p in range(0, 3):
         for node_perf in performance_per_node[node_name]:
            print "\t" + str(node_perf.throughput[p][0]),
      print
   print

   # throughput, stdev
   print("#Throughput, stdev\n" + header)
   for node_name in performance_per_node:
      print node_name,
      for p in range(0, 3):
         for node_perf in performance_per_node[node_name]:
            print "\t" + str(node_perf.throughput[p][1]),
      print
   print
# ]-- end print_results_for_bargraph/1


# ENTRY POINT
if __name__ == "__main__":
   if len(sys.argv) >= 2:
      for f in sys.argv[1:]:
         load_results(f)
      print_results_for_bargraph(len(sys.argv[1:]))
   else:
      print("Usage: %s [<experiment_directories> ...]"%(sys.argv[0])) 

