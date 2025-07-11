/**
 * pll: parallel-linked list access. support bank-aware access patterns.
 *
 * Copyright (C) 2025  Heechul Yun <heechul.yun@ku.edu> 
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
#define MAX_MLP 64
#define UNIT_SIZE 16	// per item size (bytes)
#ifdef __arm__
#  define DEFAULT_ALLOC_SIZE_KB 4096
#else
#  define DEFAULT_ALLOC_SIZE_KB 16384
#endif
#define DEFAULT_ITER 100
#define DEFAULT_MLP 1

#ifdef __LP64__
#define BITS_PER_LONG 64
#else
#define BITS_PER_LONG 32
#endif
#define BITOP_WORD(nr)		((nr) / BITS_PER_LONG)
#define PAGE_SHIFT 12

#define MAX_COLORS 64

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

static int g_debug = 0;
static int g_color[MAX_COLORS]; // not assigned
static int g_color_cnt = 0;

static int g_pagemap_fd = -1;

// static unsigned long bank_bitmask = 0x1e000; // 16|15,14,13,--| : xu4 (cortex-a15)
static unsigned long bank_bitmask = 0x7800;  // --,14,13,12|11  : pi4 (cortex-a72)

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
	unsigned long c = 0;
	for_each_set_bit(c, &mask, BITS_PER_LONG) {
		if ((paddr >> (c)) & 0x1)
			color |= (1<<idx);
		idx++;
	}
	return color;
}


size_t get_frame_number_from_pagemap(size_t value) {
    return value & ((1ULL << 54) - 1);
}

ulong get_paddr(ulong vaddr)
{
	ulong value;
	int page_size = getpagesize();
	off_t offset = (vaddr / page_size) * sizeof(value);
	assert(g_pagemap_fd >= 0);
	
	// Check if we have root permission to read frame numbers
	static int has_root_access = -1;  // Cache the result
	if (has_root_access == -1) {
		has_root_access = (geteuid() == 0) ? 1 : 0;
		if (!has_root_access) {
			printf("Warning: Running without root privileges. Physical addresses may not be accurate.\n");
		}
		return vaddr;
	} else if (has_root_access == 0) {
		// If not root, return the virtual address
		// as we cannot reliably get the physical address.
		return vaddr;
	}

	int got = pread(g_pagemap_fd, &value, sizeof(value), offset);
	assert(got == 8);

	// Check the "page present" flag.
	assert(value & (1ULL << 63));

	ulong frame_num = get_frame_number_from_pagemap(value);
	return (frame_num * page_size) | (vaddr & (page_size - 1));
}

// ----------------------------------------------
void init_pagemap() {
    g_pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
    assert(g_pagemap_fd >= 0);
}

/**************************************************************************
 * Implementation
 **************************************************************************/
int64_t run(int64_t iter, int mlp)
{
	int64_t cnt = 0;

	for (int64_t i = 0; i < iter; i++) {
		for (int j = 0; j < mlp; j++) {
			next[j] = list[j][next[j]];
		}
		cnt += mlp;
	}
	return cnt;
}

int64_t run_write(int64_t iter, int mlp)
{
	int64_t cnt = 0;

	for (int64_t i = 0; i < iter; i++) {
		for (int j = 0; j < mlp; j++) {
			list[j][next[j]+1] = 0xff; // write
			next[j] = list[j][next[j]];
		}
		cnt += mlp;
	}
	return cnt;
}

int main(int argc, char* argv[])
{
	// struct sched_param param;
        cpu_set_t cmask;
	int num_processors;
	int cpuid = 0;

	int *memchunk = NULL;
	int opt, prio;
	int i;

	long repeat = DEFAULT_ITER;
	int mlp = DEFAULT_MLP;
	int use_hugepage = 0;
	struct timespec start, end;
	int acc_type = READ;

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
		case 'a': /* set access type */
			if (!strncmp(optarg, "read", 4))
				acc_type = READ;
			else if (!strncmp(optarg, "write", 5))
				acc_type = WRITE;
			else
				exit(1);
			break;
		case 'b':
			bank_bitmask = strtol(optarg, NULL, 0);
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
		case 'e': /* select color (bank) */
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
                case 'x':
			use_hugepage = (use_hugepage) ? 0: 1;
			break;
		case 'h': /* help */
			printf("Usage: %s [options]\n", argv[0]);
			printf("Options:\n");
			printf("  -m <size>   : memory size in KB (default: %d)\n", DEFAULT_ALLOC_SIZE_KB);
			printf("  -a <type>   : access type (read|write, default: read)\n");
			printf("  -b <mask>   : bank bitmask (default: 0x%lx)\n", bank_bitmask);
			printf("  -c <cpu>    : set CPU affinity (default: 0)\n");
			printf("  -d <debug>  : debug level (default: 0)\n");
			printf("  -e <color>  : select color (bank) for coloring\n");
			printf("  -p <prio>   : set process priority\n");
			printf("  -i <iter>   : number of iterations (default: %ld)\n", (long)DEFAULT_ITER);
			printf("  -l <mlp>    : memory-level parallelism (default: %d)\n", (int)DEFAULT_MLP);
			printf("  -x          : use hugepage\n");
			exit(0);
		}

	}

	init_pagemap(); // need to open /proc/self/pagemap
	printf("g_mem_size: %d (%d KB)\n", g_mem_size, g_mem_size/1024);

	printf("sizeof(unsigned long): %d\n", (int)sizeof(unsigned long));
	printf("BITS_PER_LONG: %d\n", BITS_PER_LONG);
	unsigned long c;
	printf("\n");
	if (g_color_cnt) {
		int n_colors = 1; 
		printf("bank bitmask: 0x%lx\n", bank_bitmask);
		printf("bank bits: ");
		for_each_set_bit(c, (&bank_bitmask), BITS_PER_LONG) {
			printf("%d ", (int)c);
			n_colors *= 2; // 2^n
		}
		printf("\n");
		printf("total number of colors: %d\n", n_colors);
		printf("selected colors: ");
		for (int i = 0; i < g_color_cnt; i++) {
			printf("%d ", g_color[i]);
		}
		printf("\n");
	}
	
	srand(0);

	int ws = 0;
	int orig_ws = (g_mem_size / UNIT_SIZE);

	printf("orig_ws: %d  mlp: %d\n", orig_ws, mlp);
	
	clock_gettime(CLOCK_REALTIME, &start);

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
		ulong vaddr = (ulong)&memchunk[i*UNIT_SIZE/4];

		if (g_color_cnt > 0) {
			/* use coloring */
			for (int j = 0; j < g_color_cnt; j++) {
				ulong paddr = get_paddr(vaddr);
				if (paddr_to_color(bank_bitmask, paddr) == g_color[j]) {
					if (g_debug)
						printf("vaddr: %p paddr: %p color: %d\n",
						       (void *)vaddr,
							   (void *)paddr,
						       paddr_to_color(bank_bitmask, paddr));
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
	printf("list_len: %d\n", list_len);
	
	for (i = 0; i < ws; i++) {
		int l = i / list_len;
		int curr_idx = myvector[i] * UNIT_SIZE / 4;
		int next_idx = myvector[i+1] * UNIT_SIZE / 4;
		if ((i+1) % list_len == 0)
			next_idx = myvector[i/list_len*list_len] * UNIT_SIZE / 4;
		
		memchunk[curr_idx] = next_idx;
		
		if (i % list_len == 0) {
			list[l] = memchunk;
			next[l] = curr_idx;
			printf("list[%d]  %d\n", l,  next[l] * 4 / UNIT_SIZE);
		}
		
		// printf("%8d ->%8d\n", myvector[i], next_idx*4/UNIT_SIZE);
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
		naccess = run((int64_t)repeat * list_len, mlp);
	else
		naccess = run_write((int64_t)repeat * list_len, mlp);
	clock_gettime(CLOCK_REALTIME, &end);

	int64_t nsdiff = get_elapsed(&start, &end);
	double  avglat = (double)nsdiff/naccess;

	printf("alloc. size: %d (%d KB)\n", g_mem_size, g_mem_size/1024);
	int total_ws =  ws * UNIT_SIZE;
	printf("ws size: %d (%d KB)\n", total_ws, total_ws / 1024);
	printf("duration %.0f ns, #access %ld\n", (double)nsdiff, naccess);
	printf("Avg. latency %.2f ns\n", avglat);	
	printf("bandwidth %.2f MB/s\n", (double)64*1000*naccess/nsdiff);

	return 0;
}
