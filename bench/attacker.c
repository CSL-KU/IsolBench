/**
 * DRAM controller address mapping detector
 *
 * Copyright (C) 2013  Heechul Yun <heechul@illinois.edu> 
 * Copyright (C) 2018  Heechul Yun <heechul@ku.edu> 
 *
 * This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 *
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
#include <signal.h>
#include <sys/time.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <assert.h>
#include <sys/sysinfo.h>
#include <inttypes.h>
#include <pthread.h>

/**************************************************************************
 * Public Definitions
 **************************************************************************/
#define MAX_BIT   (18)                  // [27:23] bits are used for iterations
#define MAX_CPU   (16)
#define MAX_MLP   (6)
#define CACHE_LINE_SIZE (64)

#define MAX(a,b) ((a>b)?(a):(b))
#define MIN(a,b) ((a>b)?(b):(a))
#define CEIL(val,unit) (((val + unit - 1)/unit)*unit)

#define FATAL do { fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", \
   __LINE__, __FILE__, errno, strerror(errno)); exit(1); } while(0)

/**************************************************************************
 * Public Types
 **************************************************************************/

/**************************************************************************
 * Global Variables
 **************************************************************************/
long g_mem_size;

double g_fraction_of_physical_memory = 0.2;
int g_cache_num_ways = 16;

void *g_mapping;
ulong *g_frame_phys;

int g_cpuid = 0;
int g_pagemap_fd;

int g_debug = 0;

struct timespec g_start, g_end;
 
int g_mlp=6;
static volatile long* g_list[MAX_CPU][MAX_MLP];
volatile long long g_count[MAX_CPU];
int g_repeat = 1000000;

volatile int g_quit = 0; 
pthread_barrier_t g_barrier;

/**************************************************************************
 * Public Function Prototypes
 **************************************************************************/
uint64_t get_elapsed(struct timespec *start, struct timespec *end)
{
	uint64_t dur;
	if (start->tv_nsec > end->tv_nsec)
		dur = (uint64_t)(end->tv_sec - 1 - start->tv_sec) * 1000000000 +
			(1000000000 + end->tv_nsec - start->tv_nsec);
	else
		dur = (uint64_t)(end->tv_sec - start->tv_sec) * 1000000000 +
			(end->tv_nsec - start->tv_nsec);

	return dur;
}

// ----------------------------------------------
size_t getPhysicalMemorySize() {
	struct sysinfo info;
	sysinfo(&info);
	return (size_t) info.totalram * (size_t) info.mem_unit;
}

// ----------------------------------------------
size_t frameNumberFromPagemap(size_t value) {
	return value & ((1ULL << 54) - 1);
}

// ----------------------------------------------
ulong  getPhysicalAddr(ulong virtual_addr)
{
	u_int64_t value;
	off_t offset = (virtual_addr / 4096) * sizeof(value);
	int got = pread(g_pagemap_fd, &value, 8, offset);
	//printf("vaddr=%lu, value=0x%llx, got=%d\n", virtual_addr, value, got);
	assert(got == 8);

	// Check the "page present" flag.
	assert(value & (1ULL << 63));

	ulong frame_num = frameNumberFromPagemap(value);
	return (frame_num * 4096) | (virtual_addr & (4095));
}

// ----------------------------------------------
void setupMapping() {
	g_mem_size =
		(long)(g_fraction_of_physical_memory * getPhysicalMemorySize());
	printf("mem_size (MB): %d\n", (int)(g_mem_size / 1024 / 1024));
	
	/* map */
	g_mapping = mmap(NULL, g_mem_size, PROT_READ | PROT_WRITE,
		       MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	assert(g_mapping != (void *) -1);

	/* page virt -> phys translation table */
	g_frame_phys = (ulong *)malloc(sizeof(long) * (g_mem_size / 0x1000));
	
	/* initialize */
	for (long i = 0; i < g_mem_size; i += 0x1000) {
		ulong vaddr, paddr;
		vaddr = (ulong)(g_mapping + i);
		*((ulong *)vaddr) = 0;
		paddr = getPhysicalAddr(vaddr);
		g_frame_phys[i/0x1000] = paddr;
		// printf("vaddr-paddr: %p-%p\n", (void *)vaddr, (void *)paddr);
	}
	printf("allocation complete.\n");
}


// ----------------------------------------------
void initPagemap()
{
	g_pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	assert(g_pagemap_fd >= 0);
}

// ----------------------------------------------
long utime()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);

	return (tv.tv_sec) * 1000 + (tv.tv_usec) / 1000;
}

uint64_t nstime()
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

/**************************************************************************
 * Implementation
 **************************************************************************/

long *create_list(ulong match_mask, int max_shift, int min_count)
{
	ulong vaddr, paddr;
	int count = 0;
	long *list_curr = NULL;
	long *list_head = NULL;
	
	// printf("mask: 0x%lx, shift: %d\n", match_mask, max_shift);
	
	for (long i = 0; i < g_mem_size; i += 0x1000) {
		vaddr = (ulong)(g_mapping + i) + (match_mask & 0xFFF);
		paddr = g_frame_phys[i/0x1000] + (match_mask & 0xFFF);
		
		if (!((paddr & ((1<<max_shift) - 1)) ^ match_mask)) {
			if (*(ulong *)vaddr > 0)
				continue;
			/* found a match */
			if (g_debug) printf("vaddr-paddr: %p-%p\n", (void *)vaddr, (void *)paddr);
			count ++;
			
			if (count == 1) {
				list_head = list_curr = (long *)vaddr;
			}

			*list_curr = vaddr;
			list_curr = (long *)vaddr;
				
			if (count == min_count) {
				*list_curr = (ulong) list_head;
				// printf("#of entries in the list: %d\n", count);
				return list_head;
			}
		}
	}
	printf("failed: found (%d) / requested (%d) pages\n", count, min_count);
	return NULL;
}

long run(int cpu, long iter, int mlp)
{
	for (long i = 0; i < iter; i++) {
		switch (mlp) {
		case 10:
			g_list[cpu][9] = (long *)(*g_list[cpu][9]);
		case 9:
			g_list[cpu][8] = (long *)(*g_list[cpu][8]);
		case 8:
			g_list[cpu][7] = (long *)(*g_list[cpu][7]);
		case 7:
			g_list[cpu][6] = (long *)(*g_list[cpu][6]);
		case 6:
			g_list[cpu][5] = (long *)(*g_list[cpu][5]);
		case 5:
			g_list[cpu][4] = (long *)(*g_list[cpu][4]);
		case 4:
			g_list[cpu][3] = (long *)(*g_list[cpu][3]);
		case 3:
			g_list[cpu][2] = (long *)(*g_list[cpu][2]);
		case 2:
			g_list[cpu][1] = (long *)(*g_list[cpu][1]);
		case 1:
			g_list[cpu][0] = (long *)(*g_list[cpu][0]);
		}
		g_count[cpu] += mlp;
		

	}
	return g_count[cpu];
}


void  worker(void *param)
{
	int cpuid = (int)param;
	long int naccess = 0; 
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	
	printf("worker thread at %d begins\n", cpuid);
	
	/*synchronize with a barrier */
	pthread_barrier_wait(&g_barrier);	
	while(1) {
		naccess = run(cpuid, g_repeat, g_mlp);
	}
	printf("cpu%d: naccess=%ld\n", cpuid, naccess);
}

void quit(int param)
{
	printf("got a signal to quit\n");
	g_quit = 1; 
}

int main(int argc, char* argv[])
{
        cpu_set_t cmask;
	int num_processors, n_corun = 0;
	int opt;
	
	pthread_t tid[16]; /* thread identifier */
	pthread_attr_t attr;
	
	num_processors = sysconf(_SC_NPROCESSORS_CONF);
	pthread_attr_init(&attr);	
	
	int finish = 5;
	
	/*
	 * get command line options 
	 */
	while ((opt = getopt(argc, argv, "w:p:c:d:n:i:l:t:h")) != -1) {
		switch (opt) {
		case 'w': /* cache num ways */
			g_cache_num_ways = strtol(optarg, NULL, 0);
			break;
		case 'p': /* set memory fraction */
			g_fraction_of_physical_memory = strtof(optarg, NULL);
			break;
		case 'c': /* set CPU affinity */
			g_cpuid = strtol(optarg, NULL, 0);
			break;
		case 'd': /* debug */
			g_debug = strtol(optarg, NULL, 0);
			break;
		case 'n': /* #of co-runners */
			n_corun = strtol(optarg, NULL, 0);
			break;
		case 'i': /* iterations */
			g_repeat = strtol(optarg, NULL, 0);
			fprintf(stderr, "repeat=%d\n", g_repeat);
			finish = 0;
			break;
		case 'l': /* MLP */
			g_mlp = strtol(optarg, NULL, 0);
			break;
		case 't': /* set time in secs to run */
			finish = strtol(optarg, NULL, 0);
			break;
		case 'h':
			printf("Example: attacker -n 3 -p 0.7\n");
			break;
		}
	}
	
	initPagemap();
	setupMapping();

#if 0
	struct sched_param param;
	/* try to use a real-time scheduler*/
	param.sched_priority = 1;
	if(sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
		perror("sched_setscheduler failed");
	}
#endif
	/* launch corun worker threads */
	tid[0]= pthread_self();

	printf("ways: %d, p: %.1f, MLP: %d\n",
	       g_cache_num_ways, g_fraction_of_physical_memory, g_mlp);
	
	/* create lists */
	for (int i = 0; i < MIN(1+n_corun, num_processors); i++) {
		for (int j = 0; j < g_mlp; j++) {
			g_list[i][j] = create_list(j << 13, MAX_BIT, g_cache_num_ways*2);
		}
	}


	signal(SIGINT, &quit);
	if (finish > 0) {
		signal(SIGALRM, &quit);		
		alarm(finish);
	}

	/* barrier */
	if (n_corun > 0) pthread_barrier_init(&g_barrier, NULL, n_corun);
	
	for (int i = 1; i < MIN(1+n_corun, num_processors); i++) {
		pthread_create(&tid[i], &attr, (void *)worker, (void *)i);
                CPU_ZERO(&cmask);
		CPU_SET((g_cpuid + i) % num_processors, &cmask);
		/* thread affinity set */
		if (pthread_setaffinity_np(tid[i], sizeof(cpu_set_t), &cmask) < 0)
			perror("error");
	}
	if (n_corun > 0) pthread_barrier_wait(&g_barrier);
	
	/* start counting */
	clock_gettime(CLOCK_REALTIME, &g_start);


	/* main local thread */
	g_count[0] += run(0, g_repeat, g_mlp);


	/* cancel other threads */
	for (int i = 1; i < MIN(1+n_corun, num_processors); i++) {
		pthread_cancel(tid[i]);
	}


	/* end counting */
	clock_gettime(CLOCK_REALTIME, &g_end);


	/* calculate */
	long long g_nread = 0;
	for (int i = 0; i < MIN(1+n_corun, num_processors); i++) {
		g_nread += (g_count[i] * CACHE_LINE_SIZE);
	}
	uint64_t dur = get_elapsed(&g_start, &g_end);
	float dur_in_sec = dur_in_sec = (float)dur / 1000000000;
	
	printf("g_nread(bytes read) = %lld\n", (long long)g_nread);
	printf("elapsed = %.2f sec\n", dur_in_sec);
	float bw = (float)g_nread / dur_in_sec / 1024 / 1024;
	printf("Total B/W = %.2f MB/s | ", bw);
	printf("Latency = %.2f ns\n", (float)dur/(g_nread/CACHE_LINE_SIZE));
	
	return 0;
} 
