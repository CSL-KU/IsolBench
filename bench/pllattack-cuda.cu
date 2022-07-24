/**
 * mlp: memory-level-parallelism (MLP) detector
 *
 * Copyright (C) 2015  Heechul Yun <heechul.yun@ku.edu> 
 *
 * This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 *
 * Usage:
 *   $ sudo ./attacker4-coffeelake -m 65536 -l 12 
 */ 

/**************************************************************************
 * Conditional Compilation Options
 **************************************************************************/

/**************************************************************************
 * Included Files
 **************************************************************************/
#include <iostream>     // std::cout
#include <algorithm>    // std::random_shuffle
#include <vector>       // std::vector
#include <ctime>        // std::time
#include <cstdlib>      // std::rand, std::srand

#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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
#include <assert.h>


/**************************************************************************
 * Public Definitions
 **************************************************************************/
#define MAX_MLP 256
#define CACHE_LINE_SIZE 64
#define DEFAULT_ALLOC_SIZE_KB 65536
#define DEFAULT_ITER 100

#define PAGE_SHIFT 12

#define MAX_COLORS 64

/**************************************************************************
 * Public Types
 **************************************************************************/

/**************************************************************************
 * Global Variables
 **************************************************************************/
static int g_mem_size = (DEFAULT_ALLOC_SIZE_KB*1024);
static int* next;

static int g_debug = 0;
static int g_color[MAX_COLORS]; // not assigned
static int g_color_cnt = 0;

/**************************************************************************
 * Public Function Prototypes
 **************************************************************************/
uint64_t get_elapsed(struct timespec *start, struct timespec *end)
{
	uint64_t dur;

	dur = ((uint64_t)end->tv_sec * 1000000000 + end->tv_nsec) - 
		((uint64_t)start->tv_sec * 1000000000 + start->tv_nsec);
	return dur;
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


#define bit(addr,x) 	((addr >> (x)) & 0x1)
int paddr_to_color(unsigned long addr)
{
	return ((bit(addr, 6)^bit(addr,13))<<3|
		(bit(addr,14)^bit(addr,17))<<2|
		(bit(addr,15)^bit(addr,18))<<1|
		(bit(addr,16)^bit(addr,19)));
}

// ---------------------------------------------------------------------------
size_t libkdump_virt_to_phys(size_t virtual_address) {
  static int pagemap = -1;
  if (pagemap == -1) {
    pagemap = open("/proc/self/pagemap", O_RDONLY);
    if (pagemap < 0) {
      errno = EPERM;
      return 0;
    }
  }
  uint64_t value;
  int got = pread(pagemap, &value, 8, (virtual_address / 0x1000) * 8);
  if (got != 8) {
    errno = EPERM;
    return 0;
  }
  uint64_t page_frame_number = value & ((1ULL << 54) - 1);
  if (page_frame_number == 0) {
    errno = EPERM;
    return 0;
  }
  return page_frame_number * 0x1000 + virtual_address % 0x1000;
}


/**************************************************************************
 * Implementation
 **************************************************************************/


__global__
void gpuwrite(int* array, int *next, long iter)
{
    const uint64_t tid = blockIdx.x*blockDim.x + threadIdx.x;
    printf("next[%d]=%d iter=%d\n", (int)tid, (int)next[tid], (int)iter);
    for (long r = 0; r < iter; r++) {
        array[next[tid]+1] = 0xff;
	// if (r % 1000 == 0) printf("r=%d\n", r);
        next[tid] = array[next[tid]];
    }
}

int main(int argc, char* argv[])
{
	struct sched_param param;
        cpu_set_t cmask;
	int num_processors;
	int cpuid = 0;

	int *memchunk = NULL;
	int opt, prio;
	int i;

	long repeat = DEFAULT_ITER;
	int mlp = 1;
	struct timespec start, end;

	std::srand (0);
	std::vector<int> myvector;

	/*
	 * get command line options 
	 */
	while ((opt = getopt(argc, argv, "m:a:c:d:e:b:i:l:hx")) != -1) {
		switch (opt) {
		case 'm': /* set memory size */
			g_mem_size = 1024 * strtol(optarg, NULL, 0);
			break;
		case 'c': /* set CPU affinity */
			cpuid = strtol(optarg, NULL, 0);
			fprintf(stderr, "cpuid: %d\n", cpuid);
			num_processors = sysconf(_SC_NPROCESSORS_CONF);
			CPU_ZERO(&cmask);
			CPU_SET(cpuid % num_processors, &cmask);
			if (sched_setaffinity(0, num_processors, &cmask) < 0) {
				perror("error");
				exit(1);
			}
			else
				fprintf(stderr, "assigned to cpu %d\n", cpuid);
			break;
		case 'd': /* debug */
			g_debug = strtol(optarg, NULL, 0);
			break;
		case 'e': /* select color (dram bank) */
			g_color[g_color_cnt++] = strtol(optarg, NULL, 0);
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
			fprintf(stderr, "repeat=%ld\n", repeat);
			break;
		case 'l': /* MLP */
			mlp = strtol(optarg, NULL, 0);
			fprintf(stderr, "MLP=%d\n", mlp);
			break;
		}

	}

	printf("sizeof(unsigned long): %d\n", (int)sizeof(unsigned long));

	printf("\n");
	if (g_color_cnt) {
		printf("Colors: ");
		for (int i = 0; i < g_color_cnt; i++) {
			printf("%d ", g_color[i]);
		}
		printf("\n");
	}
	
	srand(0);

	int ws = 0;
	int orig_ws = (g_mem_size / CACHE_LINE_SIZE);

	printf("orig_ws: %d  mlp: %d\n", orig_ws, mlp);
	
	clock_gettime(CLOCK_REALTIME, &start);

	
	cudaMallocManaged(&memchunk, g_mem_size);
	cudaMallocManaged(&next, MAX_MLP * sizeof(int));
	
	/* initialize data */
	memset(memchunk, 0, g_mem_size);

	// set some values:
	int page_size = 1<<PAGE_SHIFT;

	ulong vaddr = 0;
	ulong paddr = 0;
	for (int i=0; i<orig_ws; i++) {
		vaddr = (ulong)&memchunk[i*CACHE_LINE_SIZE/4];
		if (i % (page_size/CACHE_LINE_SIZE) == 0) {
			paddr = (ulong)libkdump_virt_to_phys(vaddr);

			if (g_debug)
				printf("vaddr: %p padddr: %p color: %d\n",
				       (void *)vaddr, (void *)paddr,
				       paddr_to_color(paddr));
		} else
			paddr = paddr + CACHE_LINE_SIZE;
		
		if (g_color_cnt > 0) {
			/* use coloring */
			for (int j = 0; j < g_color_cnt; j++) {
				if (paddr_to_color(paddr) == g_color[j]) {
					myvector.push_back(i);
				}
			}

		} else {
			/* not using coloring */
			myvector.push_back(i);			
		}
	}

	// using built-in random generator:
	std::random_shuffle (myvector.begin(), myvector.end() );

	// update the workingset size
	ws = myvector.size() / mlp * mlp; 

	printf("new ws: %d\n", ws);
	int list_len = ws / mlp;
	
	for (i = 0; i < ws; i++) {
		int l = i / list_len;
		int curr_idx = myvector[i] * CACHE_LINE_SIZE / 4;
		int next_idx = myvector[i+1] * CACHE_LINE_SIZE / 4;
		if ((i+1) % list_len == 0)
			next_idx = myvector[i/list_len*list_len] * CACHE_LINE_SIZE / 4;
		memchunk[curr_idx] = next_idx;
		
		if (i % list_len == 0) {
			next[l] = curr_idx;
			// printf("next[%d]  %d\n", l,  next[l]);
		}
		
		// printf("%8d ->%8d\n", myvector[i], next_idx*4/CACHE_LINE_SIZE);
	}
	
        param.sched_priority = 1;
        if(sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
		perror("sched_setscheduler failed");
        }

	clock_gettime(CLOCK_REALTIME, &end);
	printf("Init took %.0f us\n", (double) get_elapsed(&start, &end)/1000);

	long naccess;
	int64_t nsdiff;
	double  avglat;
	int total_ws;

	int blockSize = 256;
	int numBlocks = (mlp + blockSize - 1) / blockSize;
	// long iter = repeat * list_len; 
	printf("launch the gpu kernel\n");
	clock_gettime(CLOCK_REALTIME, &start);
	gpuwrite<<<numBlocks, blockSize>>>(memchunk, next, repeat);
	cudaDeviceSynchronize();
	clock_gettime(CLOCK_REALTIME, &end);
	fprintf(stderr, "gpu kernel finishes\n");

	naccess = mlp * repeat;
	nsdiff = get_elapsed(&start, &end);
	avglat = (double)nsdiff/naccess;

	printf("alloc. size: %d (%d KB)\n", g_mem_size, g_mem_size/1024);
	total_ws =  ws * CACHE_LINE_SIZE;
	printf("ws size: %d (%d KB)\n", total_ws, total_ws / 1024);
	printf("list_len: %d (%d KB)\n", list_len, list_len * CACHE_LINE_SIZE / 1024);
	printf("mlp: %d\n", mlp);
	printf("duration %.0f ns (%.2f sec), #access %ld\n",
	       (double)nsdiff, (double)nsdiff/1000000000, naccess);
	printf("Avg. latency %.2f ns\n", avglat);	
	printf("bandwidth %.2f MB/s\n", (double)64*1000*naccess/nsdiff);
	
	return 0;
}
