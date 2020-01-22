#!/bin/bash

XLABEL="Difference with the master (%)"
YLABEL="Percentage of observations"

if [ $# -lt 2 ]; then
   echo "Usage: ./$(basename $0) <results_directory> <list> [ <of_sizes> ...]"
fi

cd $1
shift

#for S in 8 4000; do
for S in $@; do
   for T in lat thr; do

      PLOT_FILE=$(mktemp gnuplot.pXXX)
      OUT_FILE="rrbft_${S}B_${T}_distribution"

      cat << EOF > $PLOT_FILE
set term postscript eps
#set term postscript enhanced color
set output "${OUT_FILE}.eps"

set xlabel "${XLABEL}"
set ylabel "${YLABEL}"
set title "${TITLE}"

#set key left top
#set key at 3.35,5300

set yrange [0:100]

EOF

if [ $T == "lat" ]; then
   echo "set xrange [0:]" >> $PLOT_FILE
elif [ $T == "thr" ]; then
   echo "set xrange [:0]" >> $PLOT_FILE
fi

cat << EOF >> $PLOT_FILE
plot \
   "rrbft_${S}B_period_10ms_${T}.dat" title "Monitoring period = 10ms" with lines, \
   "rrbft_${S}B_period_100ms_${T}.dat" title "Monitoring period = 100ms" with lines, \
   "rrbft_${S}B_period_1s_${T}.dat" title "Monitoring period = 1s" with lines, \
   "rrbft_${S}B_period_2s_${T}.dat" title "Monitoring period = 2s" with lines

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

      #PDF_TMP="pdf_tmp.pdf"
      #pdftk ${OUT_FILE}.pdf cat 1-endE output ${PDF_TMP}
      #mv ${PDF_TMP} ${OUT_FILE}.pdf

   done
done
