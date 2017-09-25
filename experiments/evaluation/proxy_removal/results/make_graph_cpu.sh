#!/bin/sh

# specify .dat files

if [ $# -ne 1 ]; then
    echo "$0 <pdf|png>"
    exit 1
fi

FILE_X=graphX.txt

### edit the value used in the second tail
cat cpu_proxy_1.txt | awk '$0 !~ /^#|^$/{print $7/2}' | tail -n +2 | tail -n +22 > cpu_proxy_1.txt.tmp
cat cpu_proxy_2.txt | awk '$0 !~ /^#|^$/{print $7/2}' | tail -n +2 | tail -n +18 > cpu_proxy_2.txt.tmp
cat cpu_proxy_3.txt | awk '$0 !~ /^#|^$/{print $7/2}' | tail -n +2 | tail -n +15 > cpu_proxy_3.txt.tmp
cat cpu_proxy_4.txt | awk '$0 !~ /^#|^$/{print $7/2}' | tail -n +2 | tail -n +12 > cpu_proxy_4.txt.tmp

paste graphX.txt \
      cpu_proxy_1.txt.tmp \
      cpu_proxy_2.txt.tmp \
      cpu_proxy_3.txt.tmp \
      cpu_proxy_4.txt.tmp | head -n 70 > cpu_proxy.txt.tmp

cat cpu_proxy.txt.tmp | awk '{print $2 + $3 + $4 + $5}' > cpu_proxy_0.txt.tmp
paste cpu_proxy.txt.tmp cpu_proxy_0.txt.tmp > cpu_proxy.txt

LW=2
EXT=$1
FIG=graph_cpu.$EXT

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

set key top right
set key spacing 1.3
set key width 5
set key height 1

set xlabel "Time (s)"
set ylabel "CPU Utilization(%)"
set xrange [0:69]
#set yrange [0:100]

plot "< paste $FILE_X cpu_proxy_1.txt.tmp" w lines lw $LW title "client1",\
     "< paste $FILE_X cpu_proxy_2.txt.tmp" w lines lw $LW title "client2",\
     "< paste $FILE_X cpu_proxy_3.txt.tmp" w lines lw $LW title "client3",\
     "< paste $FILE_X cpu_proxy_4.txt.tmp" w lines lw $LW title "client4",\
     "< paste $FILE_X cpu_proxy_0.txt.tmp" w lines lw $LW title "total"
EOF

open $FIG

for i in `seq 1 4`
do
    FILE_CPU=cpu_proxy_$i.txt
    TMP_FILE_CPU=$FILE_CPU.tmp
    rm $TMP_FILE_CPU
done

rm *.tmp
