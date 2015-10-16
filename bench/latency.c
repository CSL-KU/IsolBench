/**
 * Latency: memory access latency measurement microbenchmark
 *
 * Copyright (C) 2015  Heechul Yun <heechul.yun@ku.edu>
 * This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 */

/**************************************************************************
 * Conditional Compilation Options
 **************************************************************************/

/**************************************************************************
 * Included Files
 **************************************************************************/
#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "list.h"

/**************************************************************************
 * Public Definitions
 **************************************************************************/
#define CACHE_LINE_SIZE 64
#ifdef __arm__
#  define DEFAULT_ALLOC_SIZE_KB 4096
#else
#  define DEFAULT_ALLOC_SIZE_KB 16384
#endif
#define DEFAULT_ITER 100

/**************************************************************************
 * Public Types
 **************************************************************************/
struct item {
	int data;
	int in_use;
	struct list_head list;
} __attribute__((aligned(CACHE_LINE_SIZE)));;

/**************************************************************************
 * Global Variables
 **************************************************************************/
int g_mem_size = DEFAULT_ALLOC_SIZE_KB*1024;

/**************************************************************************
 * Public Function Prototypes
 **************************************************************************/

/**************************************************************************
 * Implementation
 **************************************************************************/
uint64_t get_elapsed(struct timespec *start, struct timespec *end)
{
	uint64_t dur;

	dur = ((uint64_t)end->tv_sec * 1000000000 + end->tv_nsec) - 
		((uint64_t)start->tv_sec * 1000000000 + start->tv_nsec);
	return dur;
}

void usage(int argc, char *argv[])
{
	printf("Usage: $ %s [<option>]*\n\n", argv[0]);
	printf("-m: memory size in KB. deafult=%d\n", DEFAULT_ALLOC_SIZE_KB);
	printf("-s: turn on he serial access mode\n");
	printf("-c: CPU to run.\n");
	printf("-i: iterations. default=%d\n", DEFAULT_ITER);
	printf("-p: priority\n");
	printf("-h: help\n");
	exit(1);
}

int main(int argc, char* argv[])
{
	struct item *list;
	int workingset_size = 1024;
	int i, j;
	struct list_head head;
	struct list_head *pos;
	struct timespec start, end;
	uint64_t nsdiff;
	double avglat;
	uint64_t readsum = 0;
	int serial = 0;
	int repeat = DEFAULT_ITER;
	int cpuid = 0;
	struct sched_param param;
        cpu_set_t cmask;
	int num_processors;
	int opt, prio;
	/*
	 * get command line options 
	 */
	while ((opt = getopt(argc, argv, "m:sc:i:p:h")) != -1) {
		switch (opt) {
		case 'm': /* set memory size */
			g_mem_size = 1024 * strtol(optarg, NULL, 0);
			break;
		case 's': /* set access type */
			serial = 1;
			break;
		case 'c': /* set CPU affinity */
			cpuid = strtol(optarg, NULL, 0);
			num_processors = sysconf(_SC_NPROCESSORS_CONF);
			CPU_ZERO(&cmask);
			CPU_SET(cpuid % num_processors, &cmask);
			if (sched_setaffinity(0, num_processors, &cmask) < 0)
				perror("error");
			else
				fprintf(stderr, "assigned to cpu %d\n", cpuid);
			break;

		case 'p': /* set priority */
			prio = strtol(optarg, NULL, 0);
			if (setpriority(PRIO_PROCESS, 0, prio) < 0)
				perror("error");
			else
				fprintf(stderr, "assigned priority %d\n", prio);
			break;
		case 'i': /* iterations */
			repeat = strtol(optarg, NULL, 0);
			fprintf(stderr, "repeat=%d\n", repeat);
			break;
		case 'h':
			usage(argc, argv);
			break;
		}
	}

	workingset_size = g_mem_size / CACHE_LINE_SIZE;
	srand(0);

#if 0
        param.sched_priority = 1; /* 1(low) - 99(high) for SCHED_FIFO or SCHED_RR
				     0 for SCHED_OTHER or SCHED_BATCH */
        if(sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
		perror("sched_setscheduler failed");
        }
#endif

	INIT_LIST_HEAD(&head);

	/* allocate */
	list = (struct item *)malloc(sizeof(struct item) * workingset_size + CACHE_LINE_SIZE);
	for (i = 0; i < workingset_size; i++) {
		list[i].data = i;
		list[i].in_use = 0;
		INIT_LIST_HEAD(&list[i].list);
		// printf("%d 0x%x\n", list[i].data, &list[i].data);
	}
	printf("allocated: wokingsetsize=%d entries\n", workingset_size);

	/* initialize */

	int *perm = (int *)malloc(workingset_size * sizeof(int));
	for (i = 0; i < workingset_size; i++)
		perm[i] = i;

	if (!serial) {
		for (i = 0; i < workingset_size; i++) {
			int tmp = perm[i];
			int next = rand() % workingset_size;
			perm[i] = perm[next];
			perm[next] = tmp;
		}
	}
	for (i = 0; i < workingset_size; i++) {
		list_add(&list[perm[i]].list, &head);
		// printf("%d\n", perm[i]);
	}
	fprintf(stderr, "initialized.\n");

	/* actual access */
	clock_gettime(CLOCK_REALTIME, &start);
	for (j = 0; j < repeat; j++) {
		pos = (&head)->next;
		for (i = 0; i < workingset_size; i++) {
			struct item *tmp = list_entry(pos, struct item, list);
			readsum += tmp->data; // READ
			pos = pos->next;
			// printf("%d ", tmp->data, &tmp->data);
		}
	}
	clock_gettime(CLOCK_REALTIME, &end);

	nsdiff = get_elapsed(&start, &end);
	avglat = (double)nsdiff/workingset_size/repeat;
	printf("duration %.0f us\naverage %.2f ns | ", (double)nsdiff/1000, avglat);
	printf("bandwidth %.2f MB (%.2f MiB)/s\n",
	       (double)64*1000/avglat, 
	       (double)64*1000000000/avglat/1024/1024);
	printf("readsum  %lld\n", (unsigned long long)readsum);
}
