#!/bin/bash

#
# ./make_graph_comparison.sh <K :: the number of initial connections>
#

if [ $# -ne 1 ]; then
    echo "$0 <pdf|png>"
    exit 1
fi

EXT=$1
FIG=result_M.${EXT}

DIR=.

TMP_FILE="./temp.data"
./average.sh > $TMP_FILE
cat $TMP_FILE

gnuplot <<EOF
set terminal ${EXT}  font 'Arial' 22
set output "$FIG"

set rmargin 2

set grid y
#set tics   font "arial,11"
#set xlabel font "arial,11"
#set ylabel font "arial,11"
set key    font "arial,18"
set style fill solid border lc rgb "black"
set boxwidth 1

set title ""
set xlabel "# of middleboxes"
set ylabel "Latency (us)" offset 1,0,0

set key top left
#set key spacing 1.0
#set key width 5
#set key height 1.5

set xrange [0:12]
set yrange [0:800]
set parametric
set trange[0:1]

set xtics offset first 0.5

plot "$TMP_FILE" u (\$0*3+1):2:3:xtic(1) w boxerrorbars lc rgb "red"  title "Baseline",\
     "$TMP_FILE" u (\$0*3+2):4:5         w boxerrorbars lc rgb "blue" title "Dysco"
EOF

rm $TMP_FILE

open $FIG

