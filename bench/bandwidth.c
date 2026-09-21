/**
 *
 * Copyright (C) 2012  Heechul Yun <heechul@illinois.edu>
 *               2012  Zheng <zpwu@uwaterloo.ca>
 *
 * This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 *
 */

/* clang -S -mllvm --x86-asm-syntax=intel ./bandwidth.c */

/**************************************************************************
 * Conditional Compilation Options
 **************************************************************************/

/**************************************************************************
 * Included Files
 **************************************************************************/
#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/resource.h>

/**************************************************************************
 * Public Definitions
 **************************************************************************/
#define CACHE_LINE_SIZE 64	   /* cache Line size is 64 byte */
#define DEFAULT_ALLOC_SIZE_KB 16384

/**************************************************************************
 * Public Types
 **************************************************************************/
enum access_type { READ, WRITE};

/**************************************************************************
 * Global Variables
 **************************************************************************/
int64_t g_mem_size = DEFAULT_ALLOC_SIZE_KB * 1024;	   /* memory size */
int *g_mem_ptr = 0;		   /* pointer to allocated memory region */

volatile uint64_t g_nread = 0;	           /* number of bytes read */
volatile unsigned int g_start;		   /* starting time */
int cpuid = 0;

/**************************************************************************
 * Public Functions
 **************************************************************************/
unsigned int get_usecs()
{
	struct timeval         time;
	gettimeofday(&time, NULL);
	return (time.tv_sec * 1000000 +	time.tv_usec);
}

unsigned long get_default_hugepage_size_kb()
{
	FILE *meminfo;
	char name[32];
	unsigned long size_kb;

	meminfo = fopen("/proc/meminfo", "r");
	if (meminfo == NULL)
		return 0;

	while (fscanf(meminfo, "%31s %lu kB", name, &size_kb) == 2) {
		if (!strcmp(name, "Hugepagesize:")) {
			fclose(meminfo);
			return size_kb;
		}
	}

	fclose(meminfo);
	return 0;
}

static int hugepage_order(unsigned long size_kb)
{
	uint64_t size = (uint64_t)size_kb * 1024;
	int order = 0;

	if (size == 0 || (size & (size - 1)) != 0)
		return -1;

	while (size > 1) {
		size >>= 1;
		order++;
	}
	return order;
}

static void *map_hugepage(size_t length, unsigned long *mapped_size_kb)
{
	DIR *hugepage_dir;
	struct dirent *entry;
	void *mapping = MAP_FAILED;
	unsigned long best_size_kb = 0;
	int saved_errno = ENOMEM;

	hugepage_dir = opendir("/sys/kernel/mm/hugepages");
	if (hugepage_dir != NULL) {
		while ((entry = readdir(hugepage_dir)) != NULL) {
			unsigned long size_kb;
			unsigned long free_pages;
			char free_path[PATH_MAX];
			FILE *free_file;
			int order;

			if (sscanf(entry->d_name, "hugepages-%lukB", &size_kb) != 1 ||
			    size_kb <= best_size_kb)
				continue;

			order = hugepage_order(size_kb);
			if (order < 0 || (uint64_t)size_kb * 1024 > length ||
			    length % ((uint64_t)size_kb * 1024) != 0)
				continue;

			snprintf(free_path, sizeof(free_path),
			         "/sys/kernel/mm/hugepages/%s/free_hugepages",
			         entry->d_name);
			free_file = fopen(free_path, "r");
			if (free_file == NULL ||
			    fscanf(free_file, "%lu", &free_pages) != 1) {
				if (free_file != NULL)
					fclose(free_file);
				continue;
			}
			fclose(free_file);
			if (free_pages == 0)
				continue;

			mapping = mmap(NULL, length, PROT_READ | PROT_WRITE,
			               MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB |
			               (order << MAP_HUGE_SHIFT), -1, 0);
			if (mapping != MAP_FAILED) {
				best_size_kb = size_kb;
				*mapped_size_kb = size_kb;
				break;
			}
			saved_errno = errno;
		}
		closedir(hugepage_dir);
	}

	if (mapping == MAP_FAILED) {
		/* Preserve compatibility with kernels that do not expose hugetlbfs
		 * pools in sysfs. */
		mapping = mmap(NULL, length, PROT_READ | PROT_WRITE,
		               MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
		if (mapping != MAP_FAILED)
			*mapped_size_kb = get_default_hugepage_size_kb();
		else
			saved_errno = errno;
	}

	if (mapping == MAP_FAILED)
		errno = saved_errno;
	return mapping;
}

void quit(int param)
{
	float dur_in_sec;
	float bw;
	float dur = get_usecs() - g_start;
	dur_in_sec = (float)dur / 1000000;
	printf("g_nread(bytes read) = %lld\n", (long long)g_nread);
	printf("elapsed = %.2f sec ( %.0f usec )\n", dur_in_sec, dur);
	bw = (float)g_nread / dur_in_sec / 1024 / 1024;
	printf("CPU%d: B/W = %.2f MB/s | ",cpuid, bw);
	printf("CPU%d: average = %.2f ns\n", cpuid, (dur*1000)/(g_nread/CACHE_LINE_SIZE));
	exit(0);
}

int64_t bench_read()
{
	int64_t i;
	int64_t sum = 0;
	for ( i = 0; i < g_mem_size/4; i+=(CACHE_LINE_SIZE/4) ) {
		sum += g_mem_ptr[i];
	}
	g_nread += g_mem_size;
	return sum;
}

int bench_write()
{
	register int64_t i;
	for ( i = 0; i < g_mem_size/4; i+=(CACHE_LINE_SIZE/4) ) {
		g_mem_ptr[i] = i;
	}
	g_nread += g_mem_size;
	return 1;
}

void usage(int argc, char *argv[])
{
	printf("Usage: $ %s [<option>]*\n\n", argv[0]);
	printf("-m <int>[M|G] : memory size in KB. default=%d KB. appending M or G will interpret the number as MB or GB respectively.\n", DEFAULT_ALLOC_SIZE_KB);
	printf("-a <read|write>	: access type - read, write. default=read\n");
	printf("-t <int> : time to run in sec. 0 means indefinite. default=5. \n");
	printf("-x : use hugepage.\n");
	printf("-r <int> : set real-time priority. default=0; 1(low)- 99(high) for SCHED_FIFO\n");
	printf("-c <int> : CPU to run.\n");
	printf("-i <int> : iterations. 0 means intefinite. default=0\n");
	printf("-p <int> : CFS priority (nice value). -20 (highest)..19 (lowest) \n");
	printf("-h : help\n");
	printf("\nExamples: \n$ bandwidth -m 8192 -a read -t 1 -c 2\n  <- 8MB read for 1 second on CPU 2\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	int64_t sum = 0;
	unsigned finish = 5;
	int prio = 0;        
	int num_processors;
	int acc_type = READ;
	int opt;
	cpu_set_t cmask;
	int iterations = 0;
	int use_hugepage = 0;
	int i;
	struct sched_param param;

	/*
	 * get command line options 
	 */
	while ((opt = getopt(argc, argv, "m:a:t:c:i:p:r:xh")) != -1) {
		switch (opt) {
		case 'm': /* set memory size */
			if (optarg[strlen(optarg)-1] == 'G' || optarg[strlen(optarg)-1] == 'g')
				g_mem_size = 1024 * 1024 * 1024 * strtol(optarg, NULL, 0);
			else if (optarg[strlen(optarg)-1] == 'M' || optarg[strlen(optarg)-1] == 'm')
				g_mem_size = 1024 * 1024 * strtol(optarg, NULL, 0);
			else
				g_mem_size = 1024 * strtol(optarg, NULL, 0);
			break;
		case 'a': /* set access type */
			if (!strcmp(optarg, "read"))
				acc_type = READ;
			else if (!strcmp(optarg, "write"))
				acc_type = WRITE;
			else
				exit(1);
			break;
			
		case 't': /* set time in secs to run */
			finish = strtol(optarg, NULL, 0);
			break;
		case 'x':
			use_hugepage = (use_hugepage) ? 0: 1;
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
		case 'r':
			prio = strtol(optarg, NULL, 0);
			param.sched_priority = prio; /* 1(low)- 99(high) for SCHED_FIFO or SCHED_RR
						        0 for SCHED_OTHER or SCHED_BATCH */
			if(sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
				perror("sched_setscheduler failed");
			}
			break;
		case 'p': /* set priority */
			prio = strtol(optarg, NULL, 0);
			if (setpriority(PRIO_PROCESS, 0, prio) < 0)
				perror("error");
			else
				fprintf(stderr, "assigned priority %d\n", prio);
			break;
		case 'i': /* iterations */
			iterations = strtol(optarg, NULL, 0);
			break;
		case 'h': 
			usage(argc, argv);
			break;
		}
	}

	/*
	 * allocate contiguous region of memory 
	 */ 
	if (use_hugepage) {
		unsigned long hugepage_size_kb = 0;

		g_mem_ptr = (int *)map_hugepage(g_mem_size, &hugepage_size_kb);
		if ((void *)g_mem_ptr == MAP_FAILED) {
			perror("mmap with hugepage failed");
			exit(1);
		}
		if (hugepage_size_kb > 0)
			printf("Using %luKB hugepage\n", hugepage_size_kb);
		else
			printf("Using default hugepage size\n");
	} else {
		g_mem_ptr = (int *)malloc(g_mem_size);
		if (g_mem_ptr == NULL) {
			perror("alloc failed");
			exit(1);
		}
		printf("Using malloc(), not very accurate\n");
	}

	memset((char *)g_mem_ptr, 1, g_mem_size);

	for (i = 0; i < g_mem_size / sizeof(int); i++)
		g_mem_ptr[i] = i;

	/* print experiment info before starting */
	printf("memsize=%ld KB, type=%s, cpuid=%d\n",
	       g_mem_size/1024,
	       ((acc_type==READ) ?"read": "write"),
		cpuid);
	printf("stop at %d\n", finish);

	/* set signals to terminate once time has been reached */
	signal(SIGINT, &quit);
	if (finish > 0) {
		signal(SIGALRM, &quit);
		alarm(finish);
	}

	/*
	 * actual memory access
	 */
	g_start = get_usecs();
	for (i=0;; i++) {
		switch (acc_type) {
		case READ:
			sum += bench_read();
			break;
		case WRITE:
			sum += bench_write();
			break;
		}

		if (iterations > 0 && i+1 >= iterations)
			break;
	}
	printf("total sum = %ld\n", (long)sum);
	quit(0);
	return 0;
}
