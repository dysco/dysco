#!/bin/sh

if [ $# -ne 1 ]; then
    echo "$0 <dir>"
    exit 1
fi

DIR=$1

TMP_FILE=$DIR/result.txt
TMP_FILE_DYSCO=$DIR/tmp_dysco.txt
TMP_FILE_BASELINE=$DIR/tmp_baseline.txt
TMP_FILE_DYSCO2=$DIR/tmp_dysco2.txt
TMP_FILE_BASELINE2=$DIR/tmp_baseline2.txt
rm $TMP_FILE

echo $DIR

for j in 1 4 100 1000 10000
do
    rm $TMP_FILE_BASELINE2
    rm $TMP_FILE_DYSCO2
    for k in `seq 1 3`
    do
        rm $TMP_FILE_BASELINE
        rm $TMP_FILE_DYSCO
        for i in `seq 1 4`
        do
            FILE_IPERF=$DIR/baseline_${j}_${k}/iperf_$i.txt
            grep TOTAL $FILE_IPERF | cut -d " " -f 5 | awk '{sum+=$1}END{print sum/1000}' >> $TMP_FILE_BASELINE
            # DYSCO
            FILE_IPERF=$DIR/dysco_${j}_${k}/iperf_$i.txt
            grep TOTAL $FILE_IPERF | cut -d " " -f 5 | awk '{sum+=$1}END{print sum/1000}' >> $TMP_FILE_DYSCO
        done
        awk '{sum+=$1}END{print sum}' $TMP_FILE_BASELINE >> $TMP_FILE_BASELINE2
        awk '{sum+=$1}END{print sum}' $TMP_FILE_DYSCO    >> $TMP_FILE_DYSCO2

    done
    echo $j
    cat $TMP_FILE_DYSCO2
    
    echo `echo ${j}'\t';
          awk '{for(i=1;i<=NF;i++) {sum[i] += $i; sumsq[i] += ($i)^2}} END {for (i=1;i<=NF;i++) { printf "%f %f\n", sum[i]/NR, sqrt((sumsq[i]-sum[i]^2/NR)/NR)} }' $TMP_FILE_BASELINE2;
          echo '\t';
          awk '{for(i=1;i<=NF;i++) {sum[i] += $i; sumsq[i] += ($i)^2}} END {for (i=1;i<=NF;i++) { printf "%f %f\n", sum[i]/NR, sqrt((sumsq[i]-sum[i]^2/NR)/NR)} }' $TMP_FILE_DYSCO2` >> $TMP_FILE
done

rm $TMP_FILE_BASELINE
rm $TMP_FILE_BASELINE2
rm $TMP_FILE_DYSCO
rm $TMP_FILE_DYSCO2

cat $TMP_FILE
awk '{print "%f %f %f %f %f %f\n", $1, $2, $3, $4, $4/$2}' $TMP_FILE
