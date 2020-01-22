#!/bin/bash

BARGRAPH=$(dirname $0)/bargraph.pl

if [ $# -eq 1 ]; then
   EXP_DIR="$1"
else
   echo "Usage: ./$(basename $0) <experiment_directory>"
   exit -1
fi

DATA_FILE="$EXP_DIR/observed_performance.dat"

# get observed performance from the python script
$(dirname $0)/get_observed_performance_during_attack.py "$EXP_DIR" | tee "$DATA_FILE"


# simplify the format of the data file
DFILE=$(mktemp dfile.datXXX)
node=""

while read line; do
   echo $line | grep '#' &> /dev/null
   if [ $? -eq 0 ]; then
      continue
   fi

   echo $line | grep verifier &> /dev/null
   if [ $? -eq 0 ]; then
      node=$(echo $line | sed 's/verifier_\(.*\).log/\1/g')
   else
      str=$(echo $line | sed 's/PIR\([[:digit:]]\)[[:space:]]\+\([[:digit:]]\+\.[[:digit:]]\+\)[[:space:]]+\/-[[:space:]]\([[:digit:]]\+\.[[:digit:]]\+\)[[:space:]]\+\([[:digit:]]\+\)[[:space:]]+\/-[[:space:]]\([[:digit:]]\+\)/\1\t\2\t\3\t\4\t\5/g')
      echo $node $str >> $DFILE
   fi
done < "$DATA_FILE"

nodes=$(awk '{print $1}' $DFILE | sort | uniq)


###################
# bargraph. Header
HEADER=$(mktemp header.perfXXX)
cat << EOF > $HEADER
# clustered graph example from Derek Bruening's CGO 2005 talk
=cluster;PIR0;PIR1;PIR2
colors=red,med_blue,light_green
#colors=grey3,grey5,grey7
=table
yformat=%g
#max=100
=norotate
# stretch it out in x direction
extraops=set size 1.2,1
EOF


###################
# bargraph. Latency
PERF_LAT=$(mktemp lat.perfXXX)
cp $HEADER $PERF_LAT
cat << EOF >> $PERF_LAT
ylabel=Latency (ms)
#        PIR0     PIR1     PIR2
=table
EOF

for node in $nodes; do
   echo -n "$node" >> $PERF_LAT
   for pir in 0 1 2; do
      v=$(grep "$node $pir " $DFILE | awk '{print $3}')
      echo -e -n "\t$v" >> $PERF_LAT
   done
   echo "" >> $PERF_LAT
done

cat << EOF >> $PERF_LAT

=yerrorbars
EOF

for node in $nodes; do
   echo -n "$node" >> $PERF_LAT
   for pir in 0 1 2; do
      v=$(grep "$node $pir " $DFILE | awk '{print $4}')
      echo -e -n "\t$v" >> $PERF_LAT
   done
   echo "" >> $PERF_LAT
done

$BARGRAPH -pdf $PERF_LAT > "$EXP_DIR/observed_lat.pdf"


###################
# bargraph. Throughput
PERF_THR=$(mktemp thr.perfXXX)
cp $HEADER $PERF_THR
cat << EOF >> $PERF_THR
ylabel=Throughput (req/s)
#        PIR0     PIR1     PIR2
=table
EOF

for node in $nodes; do
   echo -n "$node" >> $PERF_THR
   for pir in 0 1 2; do
      v=$(grep "$node $pir " $DFILE | awk '{print $5}')
      echo -e -n "\t$v" >> $PERF_THR
   done
   echo "" >> $PERF_THR
done

cat << EOF >> $PERF_THR

=yerrorbars
EOF

for node in $nodes; do
   echo -n "$node" >> $PERF_THR
   for pir in 0 1 2; do
      v=$(grep "$node $pir " $DFILE | awk '{print $6}')
      echo -e -n "\t$v" >> $PERF_THR
   done
   echo "" >> $PERF_THR
done

$BARGRAPH -pdf $PERF_THR > "$EXP_DIR/observed_thr.pdf"


rm $PERF_THR
rm $PERF_LAT
rm $HEADER
rm $DFILE
