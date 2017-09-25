#!/bin/sh

# specify .dat files

if [ $# -ne 1 ]; then
    echo "$0 <pdf|png>"
    exit 1
fi

FILE_X=graphX.txt

## for iperf
#for i in `seq 1 4`
#do
#    FILE_IPERF=iperf_$i.txt
#    TMP_FILE_IPERF=$FILE_IPERF.tmp
#    cat $FILE_IPERF | grep SUM | sed -e 's/-/ /g' | sed -E 's/ +/ /g' \
#        | cut -d " " -f 7 | head -n 70 > $TMP_FILE_IPERF
#done
for i in `seq 1 4`
do
    FILE_IPERF=iperf_$i.txt
    TMP_FILE_IPERF=$FILE_IPERF.tmp
    echo $FILE_IPERF
    rm $TMP_FILE_IPERF
    for (( l=2; l <= 141; l++ ))
    do
        grep "^$l " $FILE_IPERF | awk '{total = total + $3} END {print total}' >> $TMP_FILE_IPERF
    done
done

paste iperf_1.txt.tmp iperf_2.txt.tmp iperf_3.txt.tmp iperf_4.txt.tmp | awk '{print $1 + $2 + $3 + $4}' > iperf_all.txt.tmp

LW=2
EXT=$1
FIG=graph_throughput.$EXT

gnuplot << EOF
set terminal $EXT size 900,450
#set terminal $EXT
set output "$FIG"

set style fill solid border lc rgb "black"
set ytics nomirror
set grid
set key    font "arial,14"
set tics   font "arial,16"
set xlabel font "arial,14"
set ylabel font "arial,14"
set tmargin 2
set bmargin 4
set lmargin 12

set key top left
set key spacing 1.3
set key width 5
set key height 1

set xlabel "Time (s)"
set ylabel "Goodput (Mbps)"
set xrange [0:140]
#set yrange [0:10000]
set ytics 2000

plot "< paste $FILE_X iperf_1.txt.tmp" w lines lw $LW title "client1",\
     "< paste $FILE_X iperf_2.txt.tmp" w lines lw $LW title "client2",\
     "< paste $FILE_X iperf_3.txt.tmp" w lines lw $LW title "client3",\
     "< paste $FILE_X iperf_4.txt.tmp" w lines lw $LW title "client4",\
     "< paste $FILE_X iperf_all.txt.tmp" w lines lw $LW title "total"
EOF

open $FIG

rm *.tmp
