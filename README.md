# IsolBench

IsolBench is a set of micro-benchmarks that are designed to 
quantify the quality of isolation of multicore systems. 

## Files

```
+- bench	isolbench and utilities
+- scripts	test scripts
+- patches	kernel patches
```
## Building IsolBench 

```
$ cd bench
$ make 

```

## Identifying Local and Global MLP

```
$ cd scripts
$ sudo ./init-hugetlbfs.sh
       --> enable huge pages
$ sudo ./mlptest.sh 10 0
       --> test up to 10 MLP, 1 instance (solo)
$ sudo ./mlptest.sh 10 1
       --> test up to 10 MLP, 2 instance (+1 corun)
```

## Evaluating Isolation Effect of Cache Partitioning

First, apply the palloc patch to your kernel (see 'patches' directory)

Run the following test script to run 6 IsolBench workloads to test
the isolation quality of your system in which the LLC is partitioned 
using PALLOC. (NOTE: you may need to adjust PALLOC setting to reflect 
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

