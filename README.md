
# FILES

```
+- bench	isolbench and utilities
+- scripts	test scripts
+- patches	kernel patches
```
# Building IsolBench 

```
$ cd bench
$ make 

```

# Identifying local and global MLP

```
$ cd scripts
$ sudo ./mlptest.sh 10 1
       --> test up to 10 MLP, w/ 1 instance 
```

# Evaluating Isolation Effect of Cache Partitioning

First, apply the palloc patch to your kernel (see 'patches' directory)

Run the following test script to run 6 IsolBench workloads to test
the isolation quality of your system in which the LLC is partitioned 
using PALLOC. (NOTE: you many need to adjust PALLOC setting to reflect 
cache configuration of your system; see 'scripts/function' file. )

```
$ cd scripts
$ sudo ./test-isolbench.sh
...
$ cat log.txt
...
bandwidth_read (512) bandwidth_read (16384)
1.94
2.51
8.42
15.25
```

