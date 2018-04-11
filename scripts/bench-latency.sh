export PATH=$PATH:../bench

echo "WS(KB)	latency(ns)"
for ws in 10 20 40 80 160 320 640 1280 2560 5120 10240; do 
	latency -c 0 -m $ws > out.txt 2> /dev/null
	VAL=`grep average out.txt | awk '{ print $2 }'`
	echo -e $ws '\t' $VAL
done 
