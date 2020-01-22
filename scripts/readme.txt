====================================================================================
Graphic which shows the percentage of observed stats (y-axis) according to the ratio between the performance of the master instance and of the backup instances.

1. edit the verifier_*.log files to comment the invalid entries, at the end of the experiment, where the observed performance may be incorrect.

2. $ ~/workspace/RRBFT/scripts/get_stats_distribution.py . lat_distribution.txt thr_distribution.txt true

3. $ gnuplot
   >> plot "./[lat+thr]_distribution.txt" using 1:3 with lines


====================================================================================
Graphic which shows the performance of the protocol instances observed by the nodes
$ ~/workspace/RRBFT/scripts/get_observed_performance_during_attack.sh .
