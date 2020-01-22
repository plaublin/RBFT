#!/bin/bash

XLABEL="Latency (ms)"
YLABEL="Percentage of observations"
TITLE=""

if [ $# -lt 1 ]; then
   echo "Usage: ./$(basename $0) <results_directory>"
   exit 0
fi

cd $1

for VERIFIER in verifier_*.log; do
   NODE="${VERIFIER#verifier_}"
   NODE="${NODE%.log}"
   TITLE="Cumulative distribution of the latencies on $NODE"
   OUT_FILE="latencies_distribution_${NODE}"

   echo "++++++++++++++++++++ $NODE ++++++++++++++++++++"

   $(dirname $0)/latency_offline_analysis.py "$VERIFIER"

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

set yrange [0:100]
set xrange [0:]

plot \
    "./distribution_${NODE}_pir0.dat" using 1:3 title "PIR0" with lines, \
    "./distribution_${NODE}_pir1.dat" using 1:3 title "PIR1" with lines, \
    "./distribution_${NODE}_pir2.dat" using 1:3 title "PIR2" with lines
EOF

   # call gnuplot
   gnuplot $PLOT_FILE

   rm $PLOT_FILE

   # convert eps to pdf
   epstopdf ${OUT_FILE}.eps
   rm ${OUT_FILE}.eps

   # pdfcrop on the figure
   pdfcrop --margins "0 0 0 0" --clip ${OUT_FILE}.pdf ${OUT_FILE}_cropped.pdf &> /dev/null
   mv ${OUT_FILE}_cropped.pdf ${OUT_FILE}.pdf
done
