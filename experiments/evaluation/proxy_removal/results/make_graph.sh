#!/bin/sh

# specify .dat files

if [ $# -ne 3 ]; then
    echo "$0 <# of cpus> <diff for cpu load> <pdf|png>"
    exit 1
fi

FILE_X=graphX.txt

if [ $1 -eq 1 ]; then
    FILE_IPERF=iperf_1.txt
    TMP_FILE_IPERF=$FILE_IPERF.tmp
    cat $FILE_IPERF | tail -n +7 | sed -e 's/-/ /g' | sed -E 's/ +/ /g' \
        | cut -d " " -f 8 > $TMP_FILE_IPERF
else
    FILE_IPERF=iperf_$1.txt
    TMP_FILE_IPERF=$FILE_IPERF.tmp
    cat $FILE_IPERF | grep SUM | sed -e 's/-/ /g' | sed -E 's/ +/ /g' \
        | cut -d " " -f 7 > $TMP_FILE_IPERF
fi

FILE_CPU=cpu_proxy_$1.txt
TMP_FILE_CPU=$FILE_CPU.tmp
cat $FILE_CPU | awk '$0 !~ /^#|^$/{print $7/2}' | tail -n +2 | tail -n +$2 > $TMP_FILE_CPU

LW=2
EXT=$3
FIG=graph_$1.$EXT

gnuplot << EOF
set terminal $EXT size 900,450
#set terminal $EXT
set output "$FIG"

set style fill solid border lc rgb "black"
set ytics nomirror
set y2tics
set grid
set key    font "arial,14"
set tics   font "arial,16"
set xlabel font "arial,16"
set ylabel font "arial,16"
set tmargin 2
set bmargin 4
set lmargin 12

set key bottom left
set key spacing 1.3
set key width 5
set key height 1

set xlabel "Time (s)"
set ylabel "Goodput (Mbps)"
set y2label "CPU Utilization (%)"
set xrange [0:59]
set yrange [0:10000]
set y2range [0:100]

plot "< paste $FILE_X $TMP_FILE_IPERF" w lines lw $LW title "Bandwidth",\
     "< paste $FILE_X $TMP_FILE_CPU"   w lines lw $LW title "CPU" axes x1y2
EOF

open $FIG

rm $TMP_FILE_IPERF
rm $TMP_FILE_CPU
