#!/bin/bash

corun=$1
acc=$2

declare -a benchmarks=(disparity localization mser multi_ncut sift stitch svm texture_synthesis tracking)

count=LLC-load-misses,LLC-loads

# ORDER:
#	Solo (No throttling/part.)
#	Corun (No throttling/part.)
# 	MBA
# 	MemGuard
#	Solo + CAT
#	Corun + CAT
# 	MBA + CAT
# 	MemGuard + CAT

# Assign cores to CLOS
#	0 => 0
#	1-23 => 1
pqos -a "core:0=0" > /dev/null
pqos -a "core:1=1-23" > /dev/null

# Keep victim core at max MBA %
pqos -e "mba:0=100" > /dev/null

for b in "${benchmarks[@]}"
do
	echo "====================================================================="
	echo $b
	echo "====================================================================="
	
	cd $b/data/sqcif
	
	pqos -e "mba:1=100" | grep "SOCKET 0"
	pqos -e "llc:0=0x7ff" | grep "SOCKET 0"
	pqos -e "llc:1=0x7ff" | grep "SOCKET 0"
	limit=50000
	echo mb 50000 $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit  > /sys/kernel/debug/memguard/limit
	
	echo "SOLO"
	perf stat -e $count chrt -f 1 taskset -c 0 make c-run | grep elapsed | awk 'NF{ print $NF }'
	echo ""
	
	
	echo "CORUN"
	for ((c=1; c<24; c+=1))
	do
		bandwidth -c $c -m $corun -t 0 -a $acc > /dev/null 2>&1 &
	done
	perf stat -e $count chrt -f 1 taskset -c 0 make c-run | grep elapsed | awk 'NF{ print $NF }'
	echo ""
	
	
	echo "MBA"
	pqos -e "mba:1=10" | grep "SOCKET 0"
	perf stat -e $count chrt -f 1 taskset -c 0 make c-run | grep elapsed | awk 'NF{ print $NF }'
	echo ""
	
	
	echo "MEMGUARD"
	pqos -e "mba:1=100" | grep "SOCKET 0"
	limit=50
	echo mb 50000 $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit  > /sys/kernel/debug/memguard/limit
	perf stat -e $count chrt -f 1 taskset -c 0 make c-run | grep elapsed | awk 'NF{ print $NF }'
	echo ""

	killall -SIGINT bandwidth


	echo "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" # Separate no CAT vs CAT tests
	echo ""
	pqos -e "mba:1=100" | grep "SOCKET 0"
	pqos -e "llc:0=0x3f" | grep "SOCKET 0"
	pqos -e "llc:1=0x7c0" | grep "SOCKET 0"
	limit=50000
	echo mb 50000 $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit  > /sys/kernel/debug/memguard/limit

	echo "SOLO + CAT"
	perf stat -e $count chrt -f 1 taskset -c 0 make c-run | grep elapsed | awk 'NF{ print $NF }'
	echo ""
	
	
	echo "CORUN + CAT"
	for ((c=1; c<24; c++))
	do
		bandwidth -c $c -m $corun -t 0 -a $acc > /dev/null 2>&1 &
	done
	perf stat -e $count chrt -f 1 taskset -c 0 make c-run | grep elapsed | awk 'NF{ print $NF }'
	echo ""
	
	
	echo "MBA + CAT"
	pqos -e "mba:1=10" | grep "SOCKET 0"
	perf stat -e $count chrt -f 1 taskset -c 0 make c-run | grep elapsed | awk 'NF{ print $NF }'
	echo ""
	
	
	echo "MEMGUARD + CAT"
	pqos -e "mba:1=100" | grep "SOCKET 0"
	limit=50
	echo mb 50000 $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit  > /sys/kernel/debug/memguard/limit
	perf stat -e $count chrt -f 1 taskset -c 0 make c-run | grep elapsed | awk 'NF{ print $NF }'
	echo ""

	killall -SIGINT bandwidth
	
	cd ../../..
	echo ""
	
	sleep 2
done 

pqos -e "mba:1=100" | grep "SOCKET 0"
pqos -e "llc:0=0x7ff" | grep "SOCKET 0"
pqos -e "llc:1=0x7ff" | grep "SOCKET 0"
