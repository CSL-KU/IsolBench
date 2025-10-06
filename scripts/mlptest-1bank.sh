#!/bin/bash
# single bank version of mlptest

#mount -t hugetlbfs none /mnt/huge
#echo 128 > /proc/sys/vm/nr_hugepages
. ./functions
. ./floatfunc

if [ -z "$1" -o -z "$2" ]; then
    echo "usage: mlptest.sh <maxmlp> <corun> [<startcore>]" >&2
    exit 1
fi

mlp=$1
corun=$2
memsize=128 # in MB

echoerr() { echo "$@" 1>&2; }

[ -z "$3" ] && st=0 || st=$3

c_start=`expr $st + 1`
c_end=`expr $st + $corun`

killall pll >& /dev/null

for l in `seq 1 $mlp`; do
    for c in `seq $c_start $c_end`; do
	    pll -c $c -l $l -i 20000 -m $memsize -f map.txt -e 0 >& /tmp/pll-$l-$c.log &
    done
    sleep 0.5
    pll -c $st -l $l -i 50 -m $memsize -f map.txt -e 0 2> /tmp/err.txt

    if grep -qi "alloc failed" /tmp/err.txt; then
        echo "Error: Failed to allocate memory for mlp $l, please allocate more hugepages." >&2
        echo "Hint: Check /proc/meminfo and init-hugetlbfs.sh" >&2
        exit 1
    fi
    killall pll >& /dev/null
    echoerr  $l `tail -n 1 /tmp/test.txt`
done  > /tmp/test.txt
BWS=`grep bandwidth /tmp/test.txt | awk '{ print $2 }'`

for b in $BWS; do
    echo $b $(float_eval "$b * ( $corun + 1)")
done > out.txt
cat out.txt

echo "corun=$1" >> out.log
awk '{ print $2 }' out.txt >> out.log
