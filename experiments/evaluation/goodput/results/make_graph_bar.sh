#!/bin/sh

#
# ./make_graph_bar.sh 
#

if [ $# -ne 2 ]; then
    echo "$0 <pdf|png> <dir>"
    exit 1
fi

EXT=$1
DIR=$2
FIG=$DIR/result.${EXT}
TMP_FILE=$DIR/result.txt
#TMP_FILE_DYSCO="./tmp_dysco.txt"
#TMP_FILE_BASELINE="./tmp_baseline.txt"
#TMP_FILE_DYSCO2="./tmp_dysco2.txt"
#TMP_FILE_BASELINE2="./tmp_baseline2.txt"
#
#rm $TMP_FILE
#for j in 1 4 100 1000 10000
##for j in 4 100 1000 10000
#do
#    rm $TMP_FILE_BASELINE2
#    rm $TMP_FILE_DYSCO2
#    for k in `seq 1 3`
#    do
#        rm $TMP_FILE_BASELINE
#        rm $TMP_FILE_DYSCO
#        
#        for i in `seq 1 4`
#        do
#            # total/client
#            # BASELINE 
#            FILE_IPERF=baseline_${j}_${k}/iperf_$i.txt
#            grep TOTAL $FILE_IPERF | cut -d " " -f 5 | awk '{sum+=$1}END{print sum/1000}' >> $TMP_FILE_BASELINE
#            # DYSCO
#            FILE_IPERF=dysco_${j}_${k}/iperf_$i.txt
#            grep TOTAL $FILE_IPERF | cut -d " " -f 5 | awk '{sum+=$1}END{print sum/1000}' >> $TMP_FILE_DYSCO
#        done
#        awk '{sum+=$1}END{print sum}' $TMP_FILE_BASELINE >> $TMP_FILE_BASELINE2
#        awk '{sum+=$1}END{print sum}' $TMP_FILE_DYSCO    >> $TMP_FILE_DYSCO2
#    done
#    
#    #echo `echo "$j \t" ; awk '{sum+=$1}END{print sum}' $TMP_FILE_BASELINE; echo "\t"; awk '{sum+=$1}END{print sum}' $TMP_FILE_DYSCO` >> $TMP_FILE
#    echo `echo ${j}'\t';
#          awk '{for(i=1;i<=NF;i++) {sum[i] += $i; sumsq[i] += ($i)^2}} END {for (i=1;i<=NF;i++) { printf "%f %f\n", sum[i]/NR, sqrt((sumsq[i]-sum[i]^2/NR)/NR)} }' $TMP_FILE_BASELINE2;
#          echo '\t';
#          awk '{for(i=1;i<=NF;i++) {sum[i] += $i; sumsq[i] += ($i)^2}} END {for (i=1;i<=NF;i++) { printf "%f %f\n", sum[i]/NR, sqrt((sumsq[i]-sum[i]^2/NR)/NR)} }' $TMP_FILE_DYSCO2` >> $TMP_FILE
#done

###### gnuplot
gnuplot <<EOF

set terminal ${EXT}  font 'Arial' 22
set output "$FIG"

set rmargin 2

set grid y
set tics   font "arial,11"
set xlabel font "arial,11"
set ylabel font "arial,11"
set key    font "arial,14"
set style fill solid border lc rgb "black"
set boxwidth 1

set title ""
set xlabel "# of TCP Sessions"
set ylabel "Goodput (Gbps)" offset 1,0,0

set key top left
#set key spacing 1.0
#set key width 5
#set key height 1.5

set xrange [0:15]
set yrange [0:11]
set parametric
set trange[0:1]

set xtics offset first 0.5

plot "$TMP_FILE" u (\$0*3+1):2:3:xtic(1) w boxerrorbars lc rgb "red"  title "Baseline",\
     "$TMP_FILE" u (\$0*3+2):4:5         w boxerrorbars lc rgb "blue" title "Dysco"

#plot "$TMP_FILE" u (\$0*3+1):2:xtic(1) w boxes lc rgb "red"  title "Baseline",\
#     "$TMP_FILE" u (\$0*3+2):3         w boxes lc rgb "blue" title "Dysco"
EOF

#rm $TMP_FILE

open $FIG
