#!/bin/bash

. ./functions

test_latency_vs_bandwidth()
{
    local size_in_kb_corun=$1
    local acc_type=$2
    local startcpu=$3

    [ -z "$acc_type" -o -z "$size_in_kb_corun" ] && error "size_in_kb_corun or acc_type is not set"
    [ -z "$startcpu" ] && startcpu=0
    endcpu=`expr $startcpu + 3`

    log_echo "latency($size_in_kb_subject) bandwidth_$acc_type ($size_in_kb_corun)"

    for cpu in `seq $startcpu $endcpu`; do 
        if [ $cpu -ne $startcpu ]; then
            # launch a co-runner
	    echo $$ > /sys/fs/cgroup/core$cpu/tasks
	    bandwidth -m $size_in_kb_corun -c $cpu -t 1000000 -a $acc_type >& /dev/null &
	    sleep 2
	    print_allocated_colors bandwidth
        fi

        # launch a subject
	echo $$ > /sys/fs/cgroup/subject/tasks
	latency -m $size_in_kb_subject -c $startcpu -i 10000 -r 1 2> /dev/null > tmpout.txt
        output=`grep average tmpout.txt | awk '{ print $2 }'`
	log_echo $output
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
    endcpu=`expr $startcpu + 3`

    log_echo "bandwidth_read ($size_in_kb_subject) bandwidth_$acc_type ($size_in_kb_corun)"

    for cpu in `seq $startcpu $endcpu`; do 
        if [ $cpu -ne $startcpu ]; then
            # launch a co-runner
	    echo $$ > /sys/fs/cgroup/core$cpu/tasks
	    bandwidth -m $size_in_kb_corun -c $cpu -t 1000000 -a $acc_type >& /dev/null &
	    sleep 2
	    print_allocated_colors bandwidth
        fi

        # launch a subject
	echo $$ > /sys/fs/cgroup/subject/tasks
        bandwidth -m $size_in_kb_subject -t 4 -c $startcpu -r 1 2> /dev/null > tmpout.txt 
        output=`grep average tmpout.txt | awk '{ print $10 }'`
	log_echo $output
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
init_system

outputfile=log.txt
startcpu=$1
[ -z "$startcpu" ] && startcpu=0

# if [ ! -d "/sys/fs/cgroup/subject" ]; then
    set_palloc_config
    set_subject_cgroup $startcpu
    set_percore_cgroup
# fi


if grep "0xc0f" /proc/cpuinfo; then
    # cortex-a15
    llc_ws=48
    dram_ws=4096
elif grep "0xc09" /proc/cpuinfo; then
    # cortex-a9
    llc_ws=48
    dram_ws=4096
elif grep "0xc07" /proc/cpuinfo; then
    # cortex-a7
    llc_ws=48
    dram_ws=4096
elif grep "0xc05" /proc/cpuinfo; then
    # cortex-a5
    llc_ws=48
    dram_ws=4096
elif grep "W3530" /proc/cpuinfo; then
    # nehalem
    llc_ws=512
    dram_ws=16384
fi

size_in_kb_subject=$llc_ws

set_pbpc
# set_shareall
# set_worst
test_latency_vs_bandwidth $dram_ws "read" $startcpu
test_bandwidth_vs_bandwidth $dram_ws "read" $startcpu
test_bandwidth_vs_bandwidth $llc_ws "read" $startcpu
test_latency_vs_bandwidth $dram_ws "write" $startcpu
test_bandwidth_vs_bandwidth $dram_ws "write" $startcpu
test_bandwidth_vs_bandwidth $llc_ws "write" $startcpu
