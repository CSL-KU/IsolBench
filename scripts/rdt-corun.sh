victim=$1
iter=$2
corun=$3
acc=$4

count=LLC-load-misses,LLC-loads

pqos -e "llc:0=0x3f"
pqos -e "llc:1=0x7c0"

perf stat -e $count chrt -f 1 bandwidth -c 0 -m $victim -t 0 -i $iter | grep average | awk 'NF{ print $(NF-1) }'
for ((i=1; i<24; i+=1))
do
	bandwidth -c $i -t 0 -m $corun -a $acc &> /dev/null &
	perf stat -e $count chrt -f 1 bandwidth -c 0 -m $victim -t 0 -i $iter | grep average | awk 'NF{ print $(NF-1) }'
done
killall bandwidth
