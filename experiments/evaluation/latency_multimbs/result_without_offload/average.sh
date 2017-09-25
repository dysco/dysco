#!/bin/zsh

# M  baseline  dysco
# 1
# 2
# 3
# 4

for N in `seq 1 4`; do
    echo -n $N
    echo -n "\t"

    for T in baseline dysco; do
        # get ave and stdev
        # http://stackoverflow.com/questions/18786073/compute-average-and-standard-deviation-with-awk
        RES=`for f in \`ls result_m${N}_${T}*.txt\`; do
            cat $f | head -n 1000 | awk '{for(i=1;i<=NF;i++) {sum[i] += $i; sumsq[i] += ($i)^2}} 
              END {for (i=1;i<=NF;i++) {
              printf "%f %f\n", sum[i]/NR, sqrt((sumsq[i]-sum[i]^2/NR)/NR)}
             }'
        done`
        
        if [ ! -z "$RES" ]; then
            # select median from three results
            echo -n `echo "$RES" | sort -n | tail -n 2 | head -n 1`
        fi
        echo -n "\t\t"
    done
    
    echo
done
