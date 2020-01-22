#!/bin/bash

if [ $# -lt 2 ]; then
   echo "Usage: ./$(basename $0) <out_file.pdf> <files> [ <to_aggregate> ... ]"
   exit 0
fi

OUT=$1
shift

PDF_TMP="pdf_tmp.pdf"
pdftk $@ cat output ${PDF_TMP}
pdftk ${PDF_TMP} cat 1-endE output ${PDF_TMP}2.pdf
mv ${PDF_TMP}2.pdf ${PDF_TMP}
pdfnup ${PDF_TMP} --nup 2x2 --frame false --outfile $OUT

rm $PDF_TMP

