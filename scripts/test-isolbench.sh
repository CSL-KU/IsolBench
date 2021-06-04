#!/bin/bash

. ./functions

test_latency_vs_bandwidth()
{
    local size_in_kb_corun=$1
    local acc_type=$2
    local startcpu=$3

    [ -z "$acc_type" -o -z "$size_in_kb_corun" ] && error "size_in_kb_corun or acc_type is not set"
    [ -z "$startcpu" ] && startcpu=0
#    [ -z "$CG_PALLOC_DIR" ] && error "CG_PALLOC_DIR is not set"

    endcpu=`expr $startcpu + 3`
    
    log_echo "latency($size_in_kb_subject) bandwidth_$acc_type ($size_in_kb_corun)"
    log_echo "avglat(ns)  L1_miss  LLC_miss   LLC_access  LLC_missrate"
    for cpu in `seq $startcpu $endcpu`; do
        if [ $cpu -ne $startcpu ]; then
            # launch a co-runner
	    [ -d "$CG_PALLOC_DIR" ] && echo $$ > $CG_PALLOC_DIR/core$cpu/tasks
	    bandwidth -m $size_in_kb_corun -c $cpu -t 1000000 -a $acc_type >& /dev/null &
	    sleep 2
	    print_allocated_colors bandwidth
        fi

        # launch a subject
	[ -d "$CG_PALLOC_DIR" ] && echo $$ > $CG_PALLOC_DIR/subject/tasks
	perf stat -e cache-misses,L1-dcache-load-misses,LLC-load-misses,LLC-loads \
	     latency -m $size_in_kb_subject -c $startcpu -i 10000 -r 1 2> tmperr.txt > tmpout.txt
	    
        output=`grep average tmpout.txt | awk '{ print $2 }'`
	perf_L1_miss=`grep L1-dcache-load-misses tmperr.txt | awk '{ print $1 }'`
	perf_LLC_miss=`grep LLC-load-misses tmperr.txt | awk '{ print $1 }'`
	perf_LLC_access=`grep LLC-loads tmperr.txt | awk '{ print $1 }'`
	perf_LLC_missrate=`grep LL-cache tmperr.txt | awk '{ print $4 }'`
	log_echo $output $perf_L1_miss $perf_LLC_miss $perf_LLC_access $perf_LLC_missrate
    # cleanup >& /dev/null
    done	
    cleanup >& /dev/null
}

test_bandwidth_vs_bandwidth()
{
    local size_in_kb_corun=$1
    local acc_type=$2
    local startcpu=$3

    [ -z "$acc_type" -o -z "$size_in_kb_corun" ] && error "size_in_kb_corun or acc_type is not set"
    [ -z "$startcpu" ] && startcpu=0
#    [ -z "$CG_PALLOC_DIR" ] && error "CG_PALLOC_DIR is not set"
    
    endcpu=`expr $startcpu + 3`

    log_echo "bandwidth_read ($size_in_kb_subject) bandwidth_$acc_type ($size_in_kb_corun)"
    log_echo "avglat(ns)  L1_miss  LLC_miss   LLC_access  LLC_missrate"
    for cpu in `seq $startcpu $endcpu`; do 
        if [ $cpu -ne $startcpu ]; then
            # launch a co-runner
	    [ -d "$CG_PALLOC_DIR" ] && echo $$ > $CG_PALLOC_DIR/core$cpu/tasks
	    bandwidth -m $size_in_kb_corun -c $cpu -t 1000000 -a $acc_type >& /dev/null &
	    sleep 2
	    print_allocated_colors bandwidth
        fi

        # launch a subject
	[ -d "$CG_PALLOC_DIR" ] && echo $$ > $CG_PALLOC_DIR/subject/tasks
	perf stat -e cache-misses,L1-dcache-load-misses,LLC-load-misses,LLC-loads \
             bandwidth -m $size_in_kb_subject -t 4 -c $startcpu -r 1 2> tmperr.txt > tmpout.txt
        output=`grep average tmpout.txt | awk '{ print $10 }'`
	perf_L1_miss=`grep L1-dcache-load-misses tmperr.txt | awk '{ print $1 }'`
	perf_LLC_miss=`grep LLC-load-misses tmperr.txt | awk '{ print $1 }'`
	perf_LLC_access=`grep LLC-loads tmperr.txt | awk '{ print $1 }'`
	perf_LLC_missrate=`grep LL-cache tmperr.txt | awk '{ print $4 }'`
	log_echo $output $perf_L1_miss $perf_LLC_miss $perf_LLC_access $perf_LLC_missrate
	
    done	
    cleanup >& /dev/null
}

print_env()
{

    echo size_in_kb_subject=$size_in_kb_subject
    echo size_in_kb_corun=$size_in_kb_corun
    echo acc_type=$acc_type
}

cleanup >& /dev/null

if [ -d "/sys/kernel/debug/palloc" ]; then
    echo "This kernel supports PALLOC. initialize."
    echo flush > /sys/kernel/debug/palloc/control

    init_system

    set_palloc_config
    set_subject_cgroup $startcpu
    set_percore_cgroup

    # cache partition setup.
    set_pbpc        # equal partition
    # set_shareall  # no partition
    # set_worst     # worst partition
fi

if grep "0xc0f" /proc/cpuinfo; then
    # cortex-a15
    llc_ws=96
    dram_ws=4096
elif grep "0xc09" /proc/cpuinfo; then
    # cortex-a9
    llc_ws=96
    dram_ws=4096
elif grep "0xc07" /proc/cpuinfo; then
    # cortex-a7
    llc_ws=48
    dram_ws=4096
elif grep "0xc05" /proc/cpuinfo; then
    # cortex-a5
    llc_ws=48
    dram_ws=4096
elif grep "0xd03" /proc/cpuinfo; then
    # cortex-a53
    llc_ws=48
    dram_ws=4096
    echo "Cortex-A53. PI3"
elif grep "W3530" /proc/cpuinfo; then
    # nehalem
    llc_ws=512
    dram_ws=16384
elif grep "Ryzen 3 2200G" /proc/cpuinfo; then
    # ryzen apu
    llc_ws=1024
    dram_ws=16384
elif grep "i7-1185GRE" /proc/cpuinfo; then
    # tigerlake
    llc_ws=2560
    dram_ws=24576
else
    error "CPU specific 'llc_ws' and 'dram_ws' variables are not set"
fi

size_in_kb_subject=$llc_ws

outputfile=log.txt
startcpu=$1
[ -z "$startcpu" ] && startcpu=0

test_latency_vs_bandwidth $llc_ws "read" $startcpu
test_latency_vs_bandwidth $llc_ws "write" $startcpu
test_latency_vs_bandwidth $dram_ws "read" $startcpu
test_bandwidth_vs_bandwidth $dram_ws "read" $startcpu
test_bandwidth_vs_bandwidth $llc_ws "read" $startcpu
test_latency_vs_bandwidth $dram_ws "write" $startcpu
test_bandwidth_vs_bandwidth $dram_ws "write" $startcpu
test_bandwidth_vs_bandwidth $llc_ws "write" $startcpu
