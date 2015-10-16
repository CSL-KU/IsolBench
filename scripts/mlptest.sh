#!/bin/bash

#mount -t hugetlbfs none /mnt/huge
#echo 128 > /proc/sys/vm/nr_hugepages
. ./functions
. ./floatfunc

if [ -z "$1" -o -z "$2" ]; then
    echo "usage: mlptest.sh <maxmlp> <corun>"
    exit 1
fi

mlp=$1
corun=$2

ALLOC_MODE="-t" # -t: huge tlbe, -x: /dev/mem"
killall latency-mlp >& /dev/null

for l in `seq 1 $mlp`; do 
    for c in `seq 1 $corun`; do
	latency-mlp -c $c -l $l -i 20000 $ALLOC_MODE >& /dev/null &
    done
    sleep 0.5
    latency-mlp -c 0 -l $l -i 100 $ALLOC_MODE
    killall latency-mlp >& /dev/null
done  > /tmp/test.txt
BWS=`grep bandwidth /tmp/test.txt | awk '{ print $2 }'`

for b in $BWS; do
    echo $b $(float_eval "$b * ( $corun + 1)")
done > out.txt
cat out.txt

echo "corun=$1" >> out.log
awk '{ print $2 }' out.txt >> out.log
