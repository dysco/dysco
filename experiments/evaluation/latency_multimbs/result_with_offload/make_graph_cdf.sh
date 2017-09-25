#!/bin/bash 

#
# ./make_graph_cdf.sh <M :: the number of middleboxes>
#

if [ $# -ne 2 ]; then
    echo "$0 <M> <pdf|png>"
    exit 1
fi

M=$1
EXT=$2
FIG=result_m${M}.${EXT}

DIR=.
FILE_X=$DIR/resultX.txt


# ex. baseline: (1, 2, 3, 4) = (127.404, 132.319, 133.564, 135.563)
if [ $M -eq 1 ]; then
    FILE_BASE=$DIR/result_m${M}_baseline_3.txt
    FILE_DYSCO=$DIR/result_m${M}_dysco_1.txt
elif [ $M -eq 2 ]; then
    FILE_BASE=$DIR/result_m${M}_baseline_2.txt
    FILE_DYSCO=$DIR/result_m${M}_dysco_1.txt
elif [ $M -eq 3 ]; then
    FILE_BASE=$DIR/result_m${M}_baseline_3.txt
    FILE_DYSCO=$DIR/result_m${M}_dysco_2.txt
elif [ $M -eq 4 ]; then
    FILE_BASE=$DIR/result_m${M}_baseline_3.txt
    FILE_DYSCO=$DIR/result_m${M}_dysco_2.txt
fi

TMP_FILE_BASE=$FILE_BASE.dat
TMP_FILE_DYSCO=$FILE_DYSCO.dat

cat $FILE_BASE | sort -n | head -n 1000 > $TMP_FILE_BASE
cat $FILE_DYSCO | sort -n | head -n 1000 > $TMP_FILE_DYSCO

gnuplot <<EOF
#set terminal postscript eps enhanced color
#set output $BASENAME.eps
set terminal ${EXT}
set output "$FIG"

#set grid y
#set tics   font "arial,16"
#set xlabel font "arial,20"
#set ylabel font "arial,20"
#set key    font "arial,20"
#set style fill solid border lc rgb "black"
#set boxwidth 1

set title ""
set xlabel "latency (us)"
set ylabel "CDF"

set key right bottom
set key spacing 1.3
set key width 5
set key height 1.5

#set xrange [0:400]
set yrange [0:1]
set parametric
set trange[0:1]

set style line 1 lc rgb "#FF0000"
set style line 2 lc rgb "#0000FF" 
set style line 3 lc rgb "#FF0000" dt 2
set style line 4 lc rgb "#0000FF" dt 4

plot "< paste $FILE_X $TMP_FILE_BASE"  u 2:1 w l ls 1 title "Baseline",\
     "< paste $FILE_X $TMP_FILE_DYSCO" u 2:1 w l ls 2 title "Dysco",\
     `cat $FILE_BASE | awk '{m+=$1} END{print m/NR;}'`,t  ls 3 title "Baseline (avg)",\
     `cat $FILE_DYSCO | awk '{m+=$1} END{print m/NR;}'`,t ls 4 title "Dysco (avg)"
EOF

rm $TMP_FILE_BASE
rm $TMP_FILE_DYSCO

open $FIG

echo -n "Baseline(avg): "
cat $FILE_BASE | awk '{m+=$1} END{print m/NR;}'

echo -n "Dysco(avg): "
cat $FILE_DYSCO | awk '{m+=$1} END{print m/NR;}'
