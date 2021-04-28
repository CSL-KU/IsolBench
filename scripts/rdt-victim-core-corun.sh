victim=$1	# size of victim in kb
vacc=$2		# access type of victim (read/write)
iter=$3		# number of victim iterations
corun=$4	# size of corunners in kb
access=$5	# access type of corunners (read/write)

# 2D matrix to hold all results
array=()

# Set RDT assignments
pqos -a "core:1=0-23"
pqos -e "llc:0=0x3f"
pqos -e "llc:1=0x7c0"
pqos -e "mba:0=100"
pqos -e "mba:1=100"

# Victim core
index=0
for ((i=0; i < 24; i++))
do
	echo ""
	echo "==================================================="
	echo "Victim Core => $i"
	echo "==================================================="
	pqos -a "core:0=$i"	# assign core under observation to CLOS 0
	
	array+=()
	
	# Run solo case
	array[$index]+="`chrt -f 1 bandwidth -c $i -i $iter -t 100 -m $victim -a $vacc 2> /dev/null | grep average | awk 'NF{ print $(NF-7) }'` "
	
	# Co-running cores
	for ((j=0; j<24; j++))
	do
		if [ "$i" -ne "$j" ]
		then
			bandwidth -c $j -t 0 -m $corun -a $access &> /dev/null &
			array[$index]+="`chrt -f 1 bandwidth -c $i -i $iter -t 100 -m $victim -a $vacc 2> /dev/null | grep average | awk 'NF{ print $(NF-7) }'` "
		fi
	done
	killall bandwidth
	echo "${array[$index]}"
		
	# Reassign core under observation to CLOS 1
	pqos -a "core:1=$i"
	
	index=$((index+1))
done

# Print entire 2D matrix
for ((i=0; i<$((index+1)); i++))
do
	echo "${array[$i]}"
done
