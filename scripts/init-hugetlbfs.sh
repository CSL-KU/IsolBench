#!/bin/bash

mount | grep hugetlbfs || mount -t hugetlbfs none /mnt/huge
echo 1024 > /proc/sys/vm/nr_hugepages

