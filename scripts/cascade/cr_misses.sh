csize=$1
cacc=$2

count=L1-dcache-load-misses,LLC-load-misses,LLC-loads

echo "==================================================="
echo "Bw$cacc ($csize) Co-runner Performance Metrics"
echo "==================================================="

echo ""
echo "bandwidth, L1_miss, LLC_miss, LLC_access, LLC_missrate"

pqos -a "core:0=0-23" &> /dev/null
pqos -a "core:1=24" &> /dev/null
pqos -a "core:2=25-47" &> /dev/null

use_cat=1
use_hp=1
use_shared=1

if [[ $use_cat == 1 ]]; then
	wrmsr -a 0xC90 0x7ff
	wrmsr -a 0xC91 0x3f
	wrmsr -a 0xC92 0x7c0
else
	wrmsr -a 0xC90 0x7ff
	wrmsr -a 0xC91 0x7ff
	wrmsr -a 0xC92 0x7ff
fi

if [[ $use_hp == 1 ]]; then
	alloc="-x"
else
	alloc=""
fi

if [[ $use_shared == 1 ]]; then
	buffer=""
else
	buffer="-o"
fi

#for ((i=25; i<48; i+=1))
for ((i=1; i<24; i+=1))
do
	output=`perf stat -e $count bandwidth-rt -c 25 -n $i -a $cacc -m $csize $alloc $buffer 2>&1` # 2> /dev/null | grep average | awk 'NF{ print $(NF-7) }'`
	bw=`echo "$output" | grep average | awk 'NF{ print $(NF-7) }'`
	l1miss=`echo "$output" | grep "L1-dcache-load-misses" | awk '{ print $1 }' | sed 's/,//g'` 
	misses=`echo "$output" | grep "LLC-load-misses" | awk '{ print $1 }' | sed 's/,//g'`
	loads=`echo "$output" | grep "LLC-loads" | awk '{ print $1 }' | sed 's/,//g'`
	missrate=`echo "$output" | grep "LLC-load-misses" | awk 'NF{ print $(NF-4) }' | sed 's/%//g'`
	echo "$bw, $l1miss, $misses, $loads, $missrate"
	#echo "$output"
	#killall bandwidth-rt
done
#killall bandwidth

