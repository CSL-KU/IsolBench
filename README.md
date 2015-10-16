
# FILES

* bench		isolbench and utilities
* scripts	test scripts
* patches	kernel patches

# Building IsolBench 

```
$ cd bench
$ make 

```

# Testing

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

