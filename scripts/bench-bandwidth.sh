export PATH=$PATH:../bench

echo "WS(KB)	bandwidth(MB/s)"
for ws in 10 20 40 80 160 320 640 1280 2560 5120 10240; do 
	bandwidth -c 0 -m $ws > out.txt 2> /dev/null
	VAL=`grep average out.txt | awk '{ print $4 }'`
	echo -e $ws '\t' $VAL
done 
