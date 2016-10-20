#!/bin/bash

. ./functions


# L2 miss - raa24
# L3 miss - r412e
perf_hwevents="instructions raa24 r412e"

get_perf_hwevent_str()
{
    local str=""
    for evt in $perf_hwevents; do
	str="$str -e ${evt}:u"
    done
    echo "$str"
}

parse_perf_log()
{
    f=$1
    val=`grep elapsed $f | awk '{ print $1 }' | sed "s/,//g"`
    if [ -f "$f" ]; then
	for counter in $perf_hwevents; do
	    [[ $counter == r* ]] && cstr=${counter:1} || cstr=$counter
	    val="$val `grep $cstr $f | awk '{ print $1 }' | sed "s/,//g"`"
	done
    fi
    echo $val
}

test_sdvbs_vs_bandwidth()
{
    local bench="$1"
    local size_in_kb_corun=$2
    local acc_type=$3
    local startcpu=$4
    local endcpu=$5

    [ -z "$acc_type" -o -z "$size_in_kb_corun" ] && error "size_in_kb_corun or acc_type is not set"
    [ -z "$startcpu" ] && startcpu=0
    [ -z "$endcpu" ] && endcpu=`expr $startcpu + 3`

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

	TMPOUT=$PWD/tmpout.txt
        PERFOUT=$PWD/perf-$bench-$cpu.txt
        pushd $SD_VBS_TOP/$bench/data/$imgsize
        ./$bench . . >& /dev/null # to warmup the cache
        taskset -c $startcpu chrt -f 99 perf stat `get_perf_hwevent_str` --repeat 3 -o $PERFOUT ./$bench . . > $TMPOUT || error "failed $bench"
        popd

	perfdat=`parse_perf_log $PERFOUT`
        runtime=`grep "Cycles" $TMPOUT | awk '{ print $4 }'` 
        # runtime=`grep "elapsed" $PERFOUT | awk '{ print $1 }'` 
        # instcnt=`grep "instructions" $PERFOUT | awk '{ print $1 }'`
        # llcload=`grep "LLC-loads" $PERFOUT | awk '{ print $1 }'`
        # llcmiss=`grep "LLC-misses" $PERFOUT | awk '{ print $1 }'`

	log_echo $runtime $perfdat
    done	
    cleanup >& /dev/null
}

test_sdvbs_vs_libquantum()
{
    local bench="$1"
    local startcpu=$2
    local endcpu=$3

    [ -z "$startcpu" ] && startcpu=0
    [ -z "$endcpu" ] && endcpu=`expr $startcpu + 3`

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

	TMPOUT=$PWD/tmpout.txt
        PERFOUT=$PWD/perf-$bench-$cpu.txt
        pushd $SD_VBS_TOP/$bench/data/$imgsize
        ./$bench . . >& /dev/null # to warmup the cache
        taskset -c $startcpu chrt -f 99 perf stat `get_perf_hwevent_str` --repeat 3 -o $PERFOUT ./$bench . . > $TMPOUT || error "failed $bench"
        popd

	perfdat=`parse_perf_log $PERFOUT`
        runtime=`grep "Cycles" $TMPOUT | awk '{ print $4 }'` 
        # runtime=`grep "elapsed" $PERFOUT | awk '{ print $1 }'` 
        # instcnt=`grep "instructions" $PERFOUT | awk '{ print $1 }'`
        # llcload=`grep "LLC-loads" $PERFOUT | awk '{ print $1 }'`
        # llcmiss=`grep "LLC-misses" $PERFOUT | awk '{ print $1 }'`

	log_echo $runtime $perfdat
    done	
    cleanup >& /dev/null
}

test_sdvbs_vs_leslie3d()
{
    local bench="$1"
    local startcpu=$2
    local endcpu=$3

    [ -z "$startcpu" ] && startcpu=0
    [ -z "$endcpu" ] && endcpu=`expr $startcpu + 3`

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

	TMPOUT=$PWD/tmpout.txt
        PERFOUT=$PWD/perf-$bench-$cpu.txt
        pushd $SD_VBS_TOP/$bench/data/$imgsize
        ./$bench . . >& /dev/null # to warmup the cache
        taskset -c $startcpu chrt -f 99 perf stat `get_perf_hwevent_str` --repeat 3 -o $PERFOUT ./$bench . . > $TMPOUT || error "failed $bench"
        popd

	perfdat=`parse_perf_log $PERFOUT`
        runtime=`grep "Cycles" $TMPOUT | awk '{ print $4 }'` 
        # runtime=`grep "elapsed" $PERFOUT | awk '{ print $1 }'` 
        # instcnt=`grep "instructions" $PERFOUT | awk '{ print $1 }'`
        # llcload=`grep "LLC-loads" $PERFOUT | awk '{ print $1 }'`
        # llcmiss=`grep "LLC-misses" $PERFOUT | awk '{ print $1 }'`

	log_echo $runtime $perfdat
    done	
    cleanup >& /dev/null
}

test_sdvbs_vs_mcf()
{
    local bench="$1"
    local startcpu=$2
    local endcpu=$3

    [ -z "$startcpu" ] && startcpu=0
    [ -z "$endcpu" ] && endcpu=`expr $startcpu + 3`

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

	TMPOUT=$PWD/tmpout.txt
        PERFOUT=$PWD/perf-$bench-$cpu.txt
        pushd $SD_VBS_TOP/$bench/data/$imgsize
        ./$bench . . >& /dev/null # to warmup the cache
        taskset -c $startcpu chrt -f 99 perf stat `get_perf_hwevent_str` --repeat 3 -o $PERFOUT ./$bench . . > $TMPOUT || error "failed $bench"
        popd

	perfdat=`parse_perf_log $PERFOUT`
        runtime=`grep "Cycles" $TMPOUT | awk '{ print $4 }'` 
        # runtime=`grep "elapsed" $PERFOUT | awk '{ print $1 }'` 
        # instcnt=`grep "instructions" $PERFOUT | awk '{ print $1 }'`
        # llcload=`grep "LLC-loads" $PERFOUT | awk '{ print $1 }'`
        # llcmiss=`grep "LLC-misses" $PERFOUT | awk '{ print $1 }'`

	log_echo $runtime $perfdat
    done	
    cleanup >& /dev/null
}

cleanup >& /dev/null
init_system

dram_ws=16384 # 16M
# dram_ws=4096 # 16M
outputfile=log.txt
SD_VBS_TOP=~/Projects/sd-vbs/benchmarks
startcpu=$1
[ -z "$startcpu" ] && startcpu=0

# if [ ! -d "/sys/fs/cgroup/subject" ]; then
    set_palloc_config
    set_subject_cgroup $startcpu
    set_percore_cgroup
# fi

imgsize=sqcif #sqcif
log_echo "imgsize: $imgsize"
set_pbpc
# for bench in disparity localization mser sift stitch svm texture_synthesis tracking multi_ncut ; do
for bench in disparity mser svm; do 
    test_sdvbs_vs_bandwidth "$bench" $dram_ws "write" $startcpu 
    #test_sdvbs_vs_libquantum "$bench" $startcpu
    #test_sdvbs_vs_leslie3d "$bench" $startcpu
    # test_sdvbs_vs_mcf "$bench" $startcpu
done 

#set_shareall
#for bench in disparity mser sift; do 
#    test_sdvbs_vs_bandwidth "$bench" $dram_ws "write" $startcpu
#done 

# set_worst
# for bench in disparity mser sift; do 
#     test_sdvbs_vs_bandwidth "$bench" $dram_ws "write" $startcpu
# done 
