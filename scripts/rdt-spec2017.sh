#!/bin/bash

corun=$1
cacc=$2

source shrc

declare -a benchmarks=(perlbench_r gcc_r mcf_r omnetpp_r xalancbmk_r x264_r deepsjeng_r leela_r exchange2_r xz_r bwaves_r cactuBSSN_r namd_r parest_r povray_r lbm_r wrf_r blender_r cam4_r imagick_r nab_r fotonik3d_r roms_r)

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
	# Print current benchmark
	echo "====================================================================="
	echo $b
	echo "====================================================================="
	
	# SOLO
	pqos -e "mba:1=100" > /dev/null
	pqos -e "llc:0=0x7ff" > /dev/null
	pqos -e "llc:1=0x7ff" > /dev/null
	limit=50000
	echo mb 50000 $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit  > /sys/kernel/debug/memguard/limit
	chrt -f 1 taskset -c 0 runcpu --config=intel $b | grep "seconds elapsed" | awk 'NF{ print $(NF-3) }'
	
	# CORUN
	for ((i=1; i<24; i+=1))
	do
		bandwidth -c $i -t 0 -m $corun -a $cacc &> /dev/null &
	done
	chrt -f 1 taskset -c 0 runcpu --config=intel $b | grep "seconds elapsed" | awk 'NF{ print $(NF-3) }'
	
	# MBA
	pqos -e "mba:1=10" > /dev/null
	chrt -f 1 taskset -c 0 runcpu --config=intel $b | grep "seconds elapsed" | awk 'NF{ print $(NF-3) }'
	
	# MemGuard
	pqos -e "mba:1=100" > /dev/null
	limit=50
	echo mb 50000 $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit  > /sys/kernel/debug/memguard/limit
	chrt -f 1 taskset -c 0 runcpu --config=intel $b | grep "seconds elapsed" | awk 'NF{ print $(NF-3) }'

	killall bandwidth
	echo ""
	
	
	# SOLO + CAT
	pqos -e "mba:1=100" > /dev/null
	pqos -e "llc:0=0x3f" > /dev/null
	pqos -e "llc:1=0x7c0" > /dev/null
	limit=50000
	echo mb 50000 $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit  > /sys/kernel/debug/memguard/limit
	chrt -f 1 taskset -c 0 runcpu --config=intel $b | grep "seconds elapsed" | awk 'NF{ print $(NF-3) }'
	
	# CORUN + CAT
	for ((i=1; i<24; i+=1))
	do
		bandwidth -c $i -t 0 -m $corun -a $cacc &> /dev/null &
	done
	chrt -f 1 taskset -c 0 runcpu --config=intel $b | grep "seconds elapsed" | awk 'NF{ print $(NF-3) }'
	
	# MBA + CAT
	pqos -e "mba:1=10" > /dev/null
	chrt -f 1 taskset -c 0 runcpu --config=intel $b | grep "seconds elapsed" | awk 'NF{ print $(NF-3) }'
	
	# MemGuard + CAT
	pqos -e "mba:1=100" > /dev/null
	limit=50
	echo mb 50000 $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit  > /sys/kernel/debug/memguard/limit
	chrt -f 1 taskset -c 0 runcpu --config=intel $b | grep "seconds elapsed" | awk 'NF{ print $(NF-3) }'

	killall bandwidth
	echo ""
	
done

pqos -e "llc:0=0x7ff" > /dev/null
pqos -e "llc:1=0x7ff" > /dev/null
