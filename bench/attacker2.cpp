/**
 * mlp: memory-level-parallelism (MLP) detector
 *
 * Copyright (C) 2015  Heechul Yun <heechul.yun@ku.edu> 
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
#define MAX_MLP 32
#define CACHE_LINE_SIZE 64
#ifdef __arm__
#  define DEFAULT_ALLOC_SIZE_KB 4096
#else
#  define DEFAULT_ALLOC_SIZE_KB 16384
#endif
#define DEFAULT_ITER 100

#ifdef __LP64
#define BITS_PER_LONG 64
#else
#define BITS_PER_LONG 32
#endif
#define BITOP_WORD(nr)		((nr) / BITS_PER_LONG)
#define PAGE_SHIFT 12

/**************************************************************************
 * Public Types
 **************************************************************************/
enum access_type { READ, WRITE};

/**************************************************************************
 * Global Variables
 **************************************************************************/
static int g_mem_size = (DEFAULT_ALLOC_SIZE_KB*1024);
static int* list[MAX_MLP];
static int next[MAX_MLP];
int g_pagemap_fd;

static unsigned long l2_bitmask   = 0x1f000; // 16 | 15,14,13,12 |  cortex-a15
static unsigned long dram_bitmask = 0x1e000; // 16 | 15,14,13,-- |  cortex-a15

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


static __always_inline unsigned long __ffs(unsigned long word)
{
        int num = 0;

#if BITS_PER_LONG == 64
        if ((word & 0xffffffff) == 0) {
                num += 32;
                word >>= 32;
        }
#endif
        if ((word & 0xffff) == 0) {
                num += 16;
                word >>= 16;
        }
        if ((word & 0xff) == 0) {
                num += 8;
                word >>= 8;
        }
        if ((word & 0xf) == 0) {
                num += 4;
                word >>= 4;
        }
        if ((word & 0x3) == 0) {
                num += 2;
                word >>= 2;
        }
        if ((word & 0x1) == 0)
                num += 1;
        return num;
}

/*
 * Find the next set bit in a memory region.
 */
unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
			    unsigned long offset)
{
	const unsigned long *p = addr + BITOP_WORD(offset);
	unsigned long result = offset & ~(BITS_PER_LONG-1);
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset %= BITS_PER_LONG;
	if (offset) {
		tmp = *(p++);
		tmp &= (~0UL << offset);
		if (size < BITS_PER_LONG)
			goto found_first;
		if (tmp)
			goto found_middle;
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}
	while (size & ~(BITS_PER_LONG-1)) {
		if ((tmp = *(p++)))
			goto found_middle;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp &= (~0UL >> (BITS_PER_LONG - size));
	if (tmp == 0UL)		/* Are any bits set? */
		return result + size;	/* Nope. */
found_middle:
	return result + __ffs(tmp);
}

#define find_first_bit(addr, size) find_next_bit((addr), (size), 0)

#define for_each_set_bit(bit, addr, size) \
	for ((bit) = find_first_bit((addr), (size));		\
	     (bit) < (size);					\
	     (bit) = find_next_bit((addr), (size), (bit) + 1))

int paddr_to_color(unsigned long mask, unsigned long paddr)
{
	int color = 0;
	int idx = 0;
	int c;
	for_each_set_bit(c, &mask, sizeof(unsigned long) * 8) {
		if ((paddr >> (c)) & 0x1)
			color |= (1<<idx);
		idx++;
	}
	return color;
}


/**************************************************************************
 * Implementation
 **************************************************************************/
int64_t run(int64_t iter, int mlp)
{
	int64_t cnt = 0;

	for (int64_t i = 0; i < iter; i++) {
		switch (mlp) {
		case 32:
			next[31] = list[31][next[31]];
		case 31:
			next[30] = list[30][next[30]];
		case 30:
			next[29] = list[29][next[29]];
		case 29:
			next[28] = list[28][next[28]];
		case 28:
			next[27] = list[27][next[27]];
		case 27:
			next[26] = list[26][next[26]];
		case 26:
			next[25] = list[25][next[25]];
		case 25:
			next[24] = list[24][next[24]];
		case 24:
			next[23] = list[23][next[23]];
		case 23:
			next[22] = list[22][next[22]];
		case 22:
			next[21] = list[21][next[21]];
		case 21:
			next[20] = list[20][next[20]];
		case 20:
			next[19] = list[19][next[19]];
		case 19:
			next[18] = list[18][next[18]];
		case 18:
			next[17] = list[17][next[17]];
		case 17:
			next[16] = list[16][next[16]];
		case 16:
			next[15] = list[15][next[15]];
		case 15:
			next[14] = list[14][next[14]];
		case 14:
			next[13] = list[13][next[13]];
		case 13:
			next[12] = list[12][next[12]];
		case 12:
			next[11] = list[11][next[11]];
		case 11:
			next[10] = list[10][next[10]];
		case 10:
			next[9] = list[9][next[9]];
		case 9:
			next[8] = list[8][next[8]];
		case 8:
			next[7] = list[7][next[7]];
		case 7:
			next[6] = list[6][next[6]];
		case 6:
			next[5] = list[5][next[5]];
		case 5:
			next[4] = list[4][next[4]];
		case 4:
			next[3] = list[3][next[3]];
		case 3:
			next[2] = list[2][next[2]];
		case 2:
			next[1] = list[1][next[1]];
		case 1:
			next[0] = list[0][next[0]];
		}
		cnt += mlp;

	}
	return cnt;
}


int main(int argc, char* argv[])
{
	struct sched_param param;
        cpu_set_t cmask;
	int num_processors;
	int cpuid = 0;

	int *memchunk = NULL;
	int opt, prio;
	int i,j,k,l;

	long repeat = DEFAULT_ITER;
	int mlp = 1;
	int use_hugepage = 0;
	struct timespec start, end;
	int acc_type = READ;

	std::srand (0);
	std::vector<int> myvector;

	initPagemap();
	
	/*
	 * get command line options 
	 */
	while ((opt = getopt(argc, argv, "m:a:c:i:l:ht")) != -1) {
		switch (opt) {
		case 'm': /* set memory size */
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
                case 't':
			use_hugepage = (use_hugepage) ? 0: 1;
			break;
		}

	}


	srand(0);

	int ws = 0;
	int orig_ws = g_mem_size / CACHE_LINE_SIZE;
	printf("orig_ws: %d  mlp: %d\n", orig_ws, mlp);
	
	clock_gettime(CLOCK_REALTIME, &start);

	for (l = 0; l < mlp; l++) {
		/* alloc memory. align to a page boundary */
		if (use_hugepage) {
			memchunk = (int *)mmap(0, 
					       g_mem_size,
					       PROT_READ | PROT_WRITE, 
					       MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, 
					       -1, 0);
			if ((void *)memchunk == MAP_FAILED) {
				perror("alloc failed");
				exit(1);
			}
		} else {
			memchunk = (int *)malloc(g_mem_size);
			if (memchunk == NULL) {
				perror("alloc failed");
				exit(1);
			}
			printf("Using malloc(), not very accurate\n");
		}

		/* initialize data */
		memset(memchunk, 0, g_mem_size);

		// set some values:
		for (int i=0; i<orig_ws; i++) {
			ulong vaddr = (ulong)&memchunk[i*CACHE_LINE_SIZE/4];
			ulong paddr = getPhysicalAddr(vaddr);
#if 0
			printf("vaddr-paddr: %p-%p bank: %d l2: %d\n",
			       (void *)vaddr, (void *)paddr,
			       paddr_to_color(dram_bitmask, paddr),
			       paddr_to_color(l2_bitmask, paddr));
#endif			
			if (paddr_to_color(dram_bitmask, paddr) == 0) // color 0 only
				myvector.push_back(i);
		}

		// using built-in random generator:
		std::random_shuffle ( myvector.begin(), myvector.end() );
		
		// update the workingset size
		ws = myvector.size();

		printf("new ws: %d\n", ws);
		
		for (i = 0; i < ws; i++) {
			int curr_idx = myvector[i] * CACHE_LINE_SIZE / 4;
			int next_idx = myvector[(i+1) % ws] * CACHE_LINE_SIZE / 4;
			memchunk[curr_idx] = next_idx;
			// printf("%8d\n", myvector[i]);
		}

		list[l] = memchunk; // &memchunk[l * CACHE_LINE_SIZE/4];
		next[l] = list[l][0];
		printf("list[%d]  0x%p\n", l, list[l]);

		myvector.clear();
	}

	if (use_hugepage) printf("Using hugetlb\n");

#if 0
        param.sched_priority = 1;
        if(sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
		perror("sched_setscheduler failed");
        }
#endif

	clock_gettime(CLOCK_REALTIME, &end);
	printf("Init took %.0f us\n", (double) get_elapsed(&start, &end)/1000);


	long naccess;
	clock_gettime(CLOCK_REALTIME, &start);
	/* actual access */
	if (acc_type == READ)
		naccess = run((int64_t)repeat * ws, mlp);
#if 0
	else
		naccess = run_write(repeat * ws, mlp);
#endif
	clock_gettime(CLOCK_REALTIME, &end);

	int64_t nsdiff = get_elapsed(&start, &end);
	// double  avglat = (double)nsdiff/naccess;

	printf("size: %d (%d KB)\n", g_mem_size, g_mem_size/1024);
	int total_ws =  mlp * ws * CACHE_LINE_SIZE;
	printf("ws size: %d (%d KB)\n", total_ws, total_ws / 1024);
	printf("duration %.0f ns, #access %ld\n", (double)nsdiff, naccess);
	printf("bandwidth %.2f MB/s\n", (double)64*1000*naccess/nsdiff);

	return 0;
}
