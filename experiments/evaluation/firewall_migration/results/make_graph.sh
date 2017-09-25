#!/bin/sh

# specify .dat files

if [ $# -ne 1 ]; then
    echo "$0 <pdf|png>"
    exit 1
fi

FILE_X=graphX.txt

## iperf
#for i in `seq 1 2`
#do
#    FILE_IPERF=iperf_$i.txt
#    TMP_FILE_IPERF=$FILE_IPERF.tmp
#    cat $FILE_IPERF | tail -n 141 | head -n 140 | sed -e 's/-/ /g' | sed -E 's/ +/ /g' \
#        | cut -d " " -f 8 > $TMP_FILE_IPERF
#done

## netperf
#for i in `seq 1 3`
#do
#    FILE_IPERF=iperf_$i.txt
#    TMP_FILE_IPERF=$FILE_IPERF.tmp
#    echo $FILE_IPERF
#    rm $TMP_FILE_IPERF
#    for (( l=2; l <= 141; l++ ))
#    do
#        grep "^$l " $FILE_IPERF | awk '{total = total + $3} END {print total/1000}' >> $TMP_FILE_IPERF
#    done
#done
#
#paste iperf_1.txt.tmp iperf_2.txt.tmp iperf_3.txt.tmp | awk '{print $1 + $2 + $3}' > iperf_all.txt.tmp
##paste iperf_1.txt.tmp iperf_2.txt.tmp | awk '{print $1 + $2}' > iperf_all.txt.tmp

LW=2
EXT=$1
FIG=graph_fw_throughput.$EXT

if [ $EXT = "png" ] ; then
    TERMINAL="set terminal $EXT font 'Arial' 18 size 900,500"
    OUTPUT="set output \"$FIG\""
else
    TERMINAL="set term postscript eps enhanced color solid 'Times-Roman' 24 size 9,5"
    OUTPUT="set output '|ps2pdf - temp.pdf'"
fi

gnuplot << EOF
$TERMINAL
$OUTPUT

set style fill solid border lc rgb "black"
set ytics nomirror
set grid
#set key    font "Arial,14"
#set tics   font "Arial,16"
#set xlabel font "Arial,16"
#set ylabel font "Arial,16"
set tmargin 0.8
set bmargin 3
set rmargin 1
set lmargin 7

set key top right

set xlabel "Time (s)"
set ylabel "Goodput (Gbps)"
set xrange [0:139]
set yrange [0:]
set ytics 1

plot "< paste $FILE_X iperf_1.txt.tmp" every 1 w lines lc rgb "blue" lw $LW title "Bundle1",\
     "< paste $FILE_X iperf_2.txt.tmp" every 1 w lines lc rgb "red" lw $LW title "Bundle2",\
     "< paste $FILE_X iperf_3.txt.tmp" every 1 w lines lc rgb "black" lw $LW title "Bundle3"
EOF

if [ $EXT = "pdf" ] ; then
    sleep 1
    pdfcrop --margins "10 8 12 8" temp.pdf $FIG
fi

open $FIG

#rm *.tmp
