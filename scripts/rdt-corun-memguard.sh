victim=$1
vacc=$2
corun=$3
cacc=$4

pqos -a "core:0=0"
pqos -a "core:1=1-23"

pqos -e "llc:0=0x3f"
pqos -e "llc:1=0x7c0"

echo "==================================================="
echo "Bw$vacc ($victim) vs. Bw$cacc ($corun)"
echo "===================================================" 

declare -a limits=(50000 10000 5000 1000 500 100 50)

for limit in "${limits[@]}"; do
	echo mb 50000 $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit $limit  > /sys/kernel/debug/memguard/limit

	echo ""
	echo "==================================================="
	echo "Co-runner B/W = $limit MB/s"
	echo "==================================================="

	perf stat -e LLC-load-misses,LLC-loads chrt -f 1 bandwidth -c 0 -i 100 -t 100 -m $victim -a $vacc 2> /dev/null | grep average | awk 'NF{ print $(NF-7) }'
	for ((i=1; i<24; i+=1))
	do
		bandwidth -c $i -t 0 -a $cacc -m $corun &> /dev/null &
		perf stat -e LLC-load-misses,LLC-loads chrt -f 1 bandwidth -c 0 -i 100 -t 100 -m $victim -a $vacc 2> /dev/null | grep average | awk 'NF{ print $(NF-7) }'
	done
	killall bandwidth
done

pqos -e "llc:0=0x7ff"
pqos -e "llc:1=0x7ff"
