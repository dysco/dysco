#!/bin/sh

# specify .dat files

if [ $# -ne 1 ]; then
    echo "$0 <pdf|png>"
    exit 1
fi

FILE_X=graphX.txt
LW=2
EXT=$1

FIG=graph_multiplot.$EXT

### for throughput
## for iperf
# for i in `seq 1 4`
# do
#     FILE_IPERF=iperf_$i.txt
#     TMP_FILE_IPERF=$FILE_IPERF.tmp
#     cat $FILE_IPERF | grep SUM | sed -e 's/-/ /g' | sed -E 's/ +/ /g' \
#         | cut -d " " -f 7 | head -n 70 > $TMP_FILE_IPERF
# done
for i in `seq 1 4`
do
    FILE_IPERF=iperf_$i.txt
    TMP_FILE_IPERF=$FILE_IPERF.tmp
    echo $FILE_IPERF
    rm $TMP_FILE_IPERF
    for (( l=2; l <= 141; l++ ))
    do
        grep "^$l " $FILE_IPERF | awk '{total = total + $3} END {print total/1000}' >> $TMP_FILE_IPERF
    done
done

paste iperf_1.txt.tmp iperf_2.txt.tmp iperf_3.txt.tmp iperf_4.txt.tmp | awk '{print $1 + $2 + $3 + $4}' > iperf_all.txt.tmp


### for cpu utlization
cat cpu_proxy_1.txt | awk '$0 !~ /^#|^$/{print $7/2}' | tail -n +2 | tail -n +22 > cpu_proxy_1.txt.tmp
cat cpu_proxy_2.txt | awk '$0 !~ /^#|^$/{print $7/2}' | tail -n +2 | tail -n +18 > cpu_proxy_2.txt.tmp
cat cpu_proxy_3.txt | awk '$0 !~ /^#|^$/{print $7/2}' | tail -n +2 | tail -n +15 > cpu_proxy_3.txt.tmp
cat cpu_proxy_4.txt | awk '$0 !~ /^#|^$/{print $7/2}' | tail -n +2 | tail -n +12 > cpu_proxy_4.txt.tmp

paste graphX.txt \
      cpu_proxy_1.txt.tmp \
      cpu_proxy_2.txt.tmp \
      cpu_proxy_3.txt.tmp \
      cpu_proxy_4.txt.tmp | head -n 140 > cpu_proxy.txt.tmp

cat cpu_proxy.txt.tmp | awk '{print $2 + $3 + $4 + $5}' > cpu_proxy_0.txt.tmp
paste cpu_proxy.txt.tmp cpu_proxy_0.txt.tmp > cpu_proxy.txt

LW=2
EXT=$1
FIG=graph_cpu.$EXT

gnuplot << EOF
#set terminal $EXT size 900,600
set terminal $EXT size 600,480
set output "$FIG"
set style fill solid border lc rgb "black"

set key font ",11"
#set tics   font "14"
#set xlabel font "12"
#set ylabel font "12"

set lmargin 12
set rmargin 4

set multiplot layout 2,1
set grid

# TOP GRAPH
set tmargin 0.8
set bmargin 0
set lmargin 8
set rmargin 2
set key top left
set xrange [0:140]
set yrange [0:10]
set xlabel ''
set ylabel "Goodput (Gbps)"
set ytics 2
set format x''
plot "< paste $FILE_X iperf_1.txt.tmp" w lines lw $LW title "Bundle1",\
     "< paste $FILE_X iperf_2.txt.tmp" w lines lw $LW title "Bundle2",\
     "< paste $FILE_X iperf_3.txt.tmp" w lines lw $LW title "Bundle3",\
     "< paste $FILE_X iperf_4.txt.tmp" w lines lw $LW title "Bundle4",\
     "< paste $FILE_X iperf_all.txt.tmp" w lines lc rgb "blue"  lw $LW title "Total"

# BOTTOM GRAPH
set tmargin 0
set bmargin 3
set lmargin 8
set rmargin 2
#set key font ",11"
set xlabel "Time (s)"
set ylabel "CPU Utilization (%)" offset 1
set xrange [0:140]
set xtics 10
set ytics 50
set format x
set yrange [0:199]
#set key top right
unset key
#plot "< paste $FILE_X cpu_proxy_1.txt.tmp" w lines  lc rgb "red" lw $LW title "Proxy1",\
#     "< paste $FILE_X cpu_proxy_2.txt.tmp" w lines  lc rgb "orange" lw $LW title "Proxy2",\
#     "< paste $FILE_X cpu_proxy_3.txt.tmp" w lines  lc rgb "yellow" lw $LW title "Proxy3",\
#     "< paste $FILE_X cpu_proxy_4.txt.tmp" w lines  lc rgb "green" lw $LW title "Proxy4",\
#     "< paste $FILE_X cpu_proxy_0.txt.tmp" w lines lc rgb "blue" lw $LW title "Total"
plot "< paste $FILE_X cpu_proxy_0.txt.tmp" w lines lc rgb "red" lw $LW title "Total"

EOF

open $FIG

#rm *.tmp
