#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# offline analysis of the latencies
# Output the latency distribution in a file whose name is like distribution_<node>_pir<p>.txt
#  argv[1]: replica file


import sys
import os
import re


BUCKET_LENGTH = 0.1 # in ms


# see http://ohuiginn.net/mt/2010/07/nested_dictionaries_in_python.html
class NestedDict(dict):
   def __getitem__(self, key):
      if key in self: return self.get(key)
      return self.setdefault(key, NestedDict())
# ]-- end class


requests_times = NestedDict()


# load the results from the file
def load_file(filename):
   global requests_times
  
   start_fetching = False
   fd = open(filename, 'r')

   for line in fd:
      # do not read comments
      if line.startswith("#"):
          continue

      if line.strip() == "Printing all latencies for off-line analysis (times in usec)":
         start_fetching = True
         continue

      if start_fetching:
         zeline = line.split()
         if len(zeline) == 5:
            try:
               pir = int(zeline[0])
               cid = int(zeline[1])
               rid = int(zeline[2])
               rcv4order = int(zeline[3])
               rcvOrdered = int(zeline[4])

               requests_times[pir][cid][rid] = (rcv4order, rcvOrdered)
            except ValueError:
               pass
      # ]-- end if
   # ]-- end for

   fd.close()
# ]-- end load_file/1

def are_there_strange_requests():
   global requests_times

   # requests never ordered?
   # requests ordered but never received?
   for p in requests_times:
      print("==== PIR %d ====="%(p))

      nb_reqs = 0
      never_ordered = 0
      ordered_but_not_received = 0
      for c in requests_times[p]:
         for r in requests_times[p][c]:
            nb_reqs += 1
            if requests_times[p][c][r][0] != 0 and requests_times[p][c][r][1] == 0:
               never_ordered += 1
            if requests_times[p][c][r][0] == 0 and requests_times[p][c][r][1] != 0:
               ordered_but_not_received += 1
      
      print("total nb requests: %d"%(nb_reqs))
      print("Requests never ordered: %d"%(never_ordered))
      print("Requests ordered but never received: %d"%(ordered_but_not_received))
   # ]-- end for
# ]-- end are_there_strange_requests/0


def get_distribution_for_one_pir(pir, requests_times_pir):
   # latence max ?
   all_reqs = []
   for c in requests_times_pir:
      for r in requests_times_pir[c]:
         if requests_times_pir[c][r][0] != 0 and requests_times_pir[c][r][1] != 0:
            lat = (requests_times_pir[c][r][1] - requests_times_pir[c][r][0]) / 1000.0
            all_reqs.append(lat)

   #print("\n===== pir %d ====="%(pir))

   all_reqs = sorted(all_reqs)
   s = len(all_reqs)
   upper = BUCKET_LENGTH
   n = 0
   D = []
   i = 0
   c = 0
   while i < s:
      if all_reqs[i] >= upper:
         p = n * 100.0 / s
         c += p
         D.append(p)
         #print("%.2f\t%.2f\t%.2f"%(upper, p, c))
         n = 0
         upper += BUCKET_LENGTH
      else:
         n += 1
         i += 1
   # ]-- end while
   
   if n > 0:
      p = n * 100.0 / s
      c += p
      D.append(p)
      #print("%.2f\t%.2f\t%.2f"%(upper, p, c))

   return D
# ]-- end are_there_strange_requests/2

# prints the results in outfile
def output_latencies_distribution(infile):
   global requests_times

   regex1 = r"""verifier_(.+).log"""
   regex1_obj = re.compile(regex1)
   match_obj = regex1_obj.search(infile)
   outfile = "distribution_"
   if match_obj == None:
      outfile += "unknown_"
   else:
      outfile += match_obj.group(1) + "_"

   # we need to construct the following array, for each pir:
   # T: latency -> percentage of requests with a latency in [latency, latency+1[
   for p in requests_times:
      D = get_distribution_for_one_pir(p, requests_times[p])
  
      outfile2 = outfile + "pir" + str(p) + ".dat"
      fd = open(outfile2, 'w')

      fd.write("#lat\tperc\tcumul_perc\n")
      upper = BUCKET_LENGTH
      c = 0
      for p in D:
         fd.write("%.2f\t%.2f\t%.2f\n"%(upper, p, c))
         c += p
         upper += BUCKET_LENGTH
   # ]-- end for

   fd.close()
# ]-- end output_latencies_distribution/1


# ENTRY POINT
if __name__ == "__main__":
    if len(sys.argv) == 2:
        load_file(sys.argv[1])
        are_there_strange_requests()
        output_latencies_distribution(sys.argv[1])
    else:
        print("Usage: %s <replica_file>"%(sys.argv[0])) 
