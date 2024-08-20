#!/bin/bash
[ -d "/mnt/huge" ] || mkdir /mnt/huge
mount | grep hugetlbfs || mount -t hugetlbfs none /mnt/huge
echo 2048 > /proc/sys/vm/nr_hugepages

