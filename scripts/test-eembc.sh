#!/bin/bash

. ./functions


test_eembc_vs_bandwidth()
{
    local bench="$1"
    local size_in_kb_corun=$2
    local acc_type=$3
    local startcpu=$4

    [ -z "$acc_type" -o -z "$size_in_kb_corun" ] && error "size_in_kb_corun or acc_type is not set"
    [ -z "$startcpu" ] && startcpu=0
    endcpu=`expr $startcpu + 3`

    log_echo "bench: $bench at $startcpu w/ bandwidth($acc_type,${size_in_kb_corun}kb)"
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
        taskset -c $startcpu chrt -f 99 $bench -autogo > tmpout.txt || error "failed $bench -autogo"
        runtime=`grep "Run Time" tmpout.txt | awk -F= '{ print $2 }' | sed 's/sec//g'`
	log_echo $runtime
    done	
    cleanup >& /dev/null
}

test_eembc_vs_libquantum()
{
    local bench="$1"
    local startcpu=$2

    [ -z "$startcpu" ] && startcpu=0
    endcpu=`expr $startcpu + 3`

    log_echo "bench: $bench at $startcpu w/ libquantum"
    for cpu in `seq $startcpu $endcpu`; do 
        if [ $cpu -ne $startcpu ]; then
            # launch a co-runner
	    echo $$ > /sys/fs/cgroup/core$cpu/tasks
	    #bandwidth -m $size_in_kb_corun -c $cpu -t 1000000 -a $acc_type >& /dev/null &
	    libquantum 1397 8 >& /dev/numm &
	    sleep 2
            print_allocated_colors libquantum
        fi

        # launch a subject
	echo $$ > /sys/fs/cgroup/subject/tasks
        taskset -c $startcpu chrt -f 99 $bench -autogo > tmpout.txt || error "failed $bench -autogo"
        runtime=`grep "Run Time" tmpout.txt | awk -F= '{ print $2 }' | sed 's/sec//g'`
	log_echo $runtime
    done	
    cleanup >& /dev/null
}

test_eembc_vs_leslie3d()
{
    local bench="$1"
    local startcpu=$2

    [ -z "$startcpu" ] && startcpu=0
    endcpu=`expr $startcpu + 3`

    log_echo "bench: $bench at $startcpu w/ leslie3d"
    for cpu in `seq $startcpu $endcpu`; do 
        if [ $cpu -ne $startcpu ]; then
            # launch a co-runner
	    echo $$ > /sys/fs/cgroup/core$cpu/tasks
	    leslie3d < leslie3d.in >& /dev/numm &
	    sleep 2
            print_allocated_colors leslie3d
        fi

        # launch a subject
	echo $$ > /sys/fs/cgroup/subject/tasks
        taskset -c $startcpu chrt -f 99 $bench -autogo > tmpout.txt || error "failed $bench -autogo"
        runtime=`grep "Run Time" tmpout.txt | awk -F= '{ print $2 }' | sed 's/sec//g'`
	log_echo $runtime
    done	
    cleanup >& /dev/null
}

test_eembc_vs_mcf()
{
    local bench="$1"
    local startcpu=$2

    [ -z "$startcpu" ] && startcpu=0
    endcpu=`expr $startcpu + 3`

    log_echo "bench: $bench at $startcpu w/ mcf"
    for cpu in `seq $startcpu $endcpu`; do 
        if [ $cpu -ne $startcpu ]; then
            # launch a co-runner
	    echo $$ > /sys/fs/cgroup/core$cpu/tasks
	    mcf inp.in >& /dev/numm &
	    sleep 2
            print_allocated_colors mcf
        fi

        # launch a subject
	echo $$ > /sys/fs/cgroup/subject/tasks
        taskset -c $startcpu chrt -f 99 $bench -autogo > tmpout.txt || error "failed $bench -autogo"
        runtime=`grep "Run Time" tmpout.txt | awk -F= '{ print $2 }' | sed 's/sec//g'`
	log_echo $runtime
    done	
    cleanup >& /dev/null
}

cleanup >& /dev/null
init_system

dram_ws=16384 # 16M
outputfile=log.txt
startcpu=$1
[ -z "$startcpu" ] && startcpu=0

# if [ ! -d "/sys/fs/cgroup/subject" ]; then
    set_palloc_config
    set_subject_cgroup $startcpu
    set_percore_cgroup
# fi

set_pbpc
# set_shareall
# set_worst
while read bench; do 
    test_eembc_vs_bandwidth "$bench" $dram_ws "write" $startcpu
    #test_eembc_vs_libquantum "$bench" $startcpu
    #test_eembc_vs_leslie3d "$bench" $startcpu
    # test_eembc_vs_mcf "$bench" $startcpu
done < eembc-high.txt
