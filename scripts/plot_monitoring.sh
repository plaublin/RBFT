#!/bin/bash
#
#
# Plot the monitoring stats
# More precisely, given an experiment directory, get the stats
# and generate a graph per verifier.
# The results are found in the experiment directory.
#  $1: experiment directory

if [ $# -eq 1 ]; then
   EXP_DIR="$1"
else
   echo "Usage: ./$(basename $0) <exp_dir>"
   exit 0
fi


# Args:
#  $1: verifier stats datafile
#  $2: title
#  $3: out_file
#  $4: thr or lat?
#  $5: Y_RANGE_MIN
#  $6: Y_RANGE_MAX
function plot_for_1_verifier {
   if [ $# -ne 6 ]; then
      echo "Usage: plot_for_1_verifier <verifier_stats_datafile> <title> <out_file> <thr_or_lat> <y_range_min> <y_range_max>"
      echo $@
      return
   fi

   STATS_DATAFILE="$1"
   TITLE="$2"
   OUT_FILE="$3"
   THR_OR_LAT="$4"
   Y_RANGE_MIN=$5
   Y_RANGE_MAX=$6

   XLABEL="Time (s)"
   YLABEL="Monitoring difference (%)"

   PLOT_FILE=$(mktemp gnuplot.pXXX)
   
   cat << EOF > $PLOT_FILE
#set term postscript eps
set term postscript enhanced color
set output "${OUT_FILE}.eps"

set xlabel "${XLABEL}"
set ylabel "${YLABEL}"
set title "${TITLE}"

#set key left top
#set key at 3.35,5300

set xrange [0:]
set yrange [${Y_RANGE_MIN}:${Y_RANGE_MAX}]

EOF

   # we need the min and the max for that verifier
   tmp=$(mktemp v.tmpXXX)

   if [ $THR_OR_LAT = "thr" ]; then
      echo -n "plot \"${STATS_DATAFILE}\" using 2:4 title \"\" with lines" >> $PLOT_FILE
      cat ${STATS_DATAFILE} | grep -v '^#' | awk '{print $4}' | sort -n > $tmp
   elif [ $THR_OR_LAT = "lat" ]; then
      echo -n "plot \"${STATS_DATAFILE}\" using 2:3 title \"\" with lines" >> $PLOT_FILE
      cat ${STATS_DATAFILE} | grep -v '^#' | awk '{print $3}' | sort -n > $tmp
   fi

   min=$(head -n 1 $tmp)
   max=$(tail -n 1 $tmp)
   echo ", $max title \"\" with lines ls 2 lw 2, $min title \"\" with lines ls 2 lw 2" >> $PLOT_FILE

   rm $tmp

   echo "" >> $PLOT_FILE

   # call gnuplot
   gnuplot $PLOT_FILE

   rm $PLOT_FILE

   # convert eps to pdf
   epstopdf ${OUT_FILE}.eps
   rm ${OUT_FILE}.eps

   # pdfcrop on the figure
   pdfcrop --margins "0 0 0 0" --clip ${OUT_FILE}.pdf ${OUT_FILE}_cropped.pdf &> /dev/null
   mv ${OUT_FILE}_cropped.pdf ${OUT_FILE}.pdf
}


##############
# 1. Get stats

ALL_STATS_DATAFILE=""
for verifier in ${EXP_DIR}/verifier_sci*.log; do
   # get the monitoring stats
   STATS_DATAFILE="stats_$(basename $verifier)"
   STATS_DATAFILE="${STATS_DATAFILE%log}dat"
   ALL_STATS_DATAFILE="${ALL_STATS_DATAFILE} ${STATS_DATAFILE}"

   $(dirname $0)/get_monitoring_stats.py $verifier ${STATS_DATAFILE}
done


ALL_PLOTS=""

##############
# 2. Plot throughput

# get the min and the max on all data files
tmp=$(mktemp v.tmpXXX)
cat ${ALL_STATS_DATAFILE} | grep -v '^#' | awk '{print $4}' | sort -n > $tmp
min=$(head -n 1 $tmp)
max=$(tail -n 1 $tmp)
Y_MIN=$(echo "$min - 10/100*$min" | bc -l)
Y_MAX=$(echo "$max + 10/100*$max" | bc -l)
rm $tmp


# plot
for f in $(echo $ALL_STATS_DATAFILE | tr ' ' '\n'); do
   # get monitoring period
   f2=${f%dat}log
   f2=${f2#stats_}
   mon=$(echo "scale=0; $(grep monitoring_period ${EXP_DIR}/$f2 | awk '{print $5}')/1000" | bc -l)

   title="${f%.dat}, monitoring period = $mon ms"
   title="verifier ${title#stats_verifier_}"

   plot_for_1_verifier ${f} "$title" ${f%.dat}_thr thr $Y_MIN $Y_MAX
   ALL_PLOTS="${ALL_PLOTS} ${f%.dat}_thr.pdf"
done


##############
# 3. Plot latency

# get the min and the max on all data files
tmp=$(mktemp v.tmpXXX)
cat ${ALL_STATS_DATAFILE} | grep -v '^#' | awk '{print $3}' | sort -n > $tmp
min=$(head -n 1 $tmp)
max=$(tail -n 1 $tmp)
Y_MIN=$(echo "$min - 10/100*$min" | bc -l)
Y_MAX=$(echo "$max + 10/100*$max" | bc -l)
rm $tmp


# plot
for f in $(echo $ALL_STATS_DATAFILE | tr ' ' '\n'); do
   # get monitoring period
   f2=${f%dat}log
   f2=${f2#stats_}
   mon=$(echo "scale=0; $(grep monitoring_period ${EXP_DIR}/$f2 | awk '{print $5}')/1000" | bc -l)

   title="${f%.dat}, monitoring period = $mon ms"
   title="verifier ${title#stats_verifier_}"

   plot_for_1_verifier ${f} "$title" ${f%.dat}_lat lat $Y_MIN $Y_MAX
   ALL_PLOTS="${ALL_PLOTS} ${f%.dat}_lat.pdf"
done


##############
# 4. Save files

# save results in the EXP dir
mv ${ALL_STATS_DATAFILE} ${EXP_DIR}/
mv ${ALL_PLOTS} ${EXP_DIR}/
