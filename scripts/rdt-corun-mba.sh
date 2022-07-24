victim=$1
iter=$2
corun=$3
access=$4
declare -a percents=(10 20 30 40 50 60 90 100)

pqos -e "llc:0=0x3"
pqos -e "llc:1=0x7fc"
pqos -e "mba:0=100"

for p in "${percents[@]}"
do
	echo ""
	echo "==================================================="
	echo "MBA => $p%"
	echo "==================================================="
	pqos -e "mba:1=$p" &> /dev/null

	
	chrt -f 1 bandwidth -c 0 -i $iter -t 0 -m $victim 2> /dev/null | grep average | awk 'NF{ print $(NF-7) }'
	for ((i=1; i<24; i+=1))
	do
		bandwidth -c $i -t 0 -a $access -m $corun &> /dev/null &
		chrt -f 1 bandwidth -c 0 -i $iter -t 0 -m $victim 2> /dev/null | grep average | awk 'NF{ print $(NF-7) }'
	done
	killall bandwidth
done