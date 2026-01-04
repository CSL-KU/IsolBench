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
#include <algorithm>    // std::shuffle
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
#include <random>
#include <getopt.h>
#include <string>
#include <sstream>

/**************************************************************************
 * Public Definitions
 **************************************************************************/
#define MAX_MLP 64
#define DEFAULT_ALLOC_SIZE_KB 16384
#define DEFAULT_ITER 100
#define DEFAULT_MLP 1
#define LINE_SIZE 64

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
static int64_t g_mem_size = (DEFAULT_ALLOC_SIZE_KB*1024);
static int64_t g_unit_size = 64; // 64B
static int64_t* list[MAX_MLP];
static int64_t next[MAX_MLP];

static int g_debug = 0;
static int g_color[MAX_COLORS]; // not assigned
static int g_color_cnt = 0;

static int g_pagemap_fd = -1;

// static unsigned long bank_bitmask = 0x1e000; // 16|15,14,13,--| : xu4 (cortex-a15)
static unsigned long bank_bitmask = 0x7800;  // --,14,13,12|11  : pi4 (cortex-a72)

// XOR mapping functions
static std::vector<std::vector<int>> g_dram_functions;
static std::vector<std::vector<int>> g_llc_functions;
static char* g_dram_map_file = nullptr;
static char* g_llc_map_file = nullptr;

// Selected banks/slices
static std::vector<int> g_selected_dram_banks;
static std::vector<int> g_selected_llc_slices;

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

void parse_list(char* str, std::vector<int>& list) {
    char* token = strtok(str, ",");
    while (token != nullptr) {
        if (strchr(token, '-')) {
            int start, end;
            sscanf(token, "%d-%d", &start, &end);
            for (int i = start; i <= end; i++) {
                list.push_back(i);
            }
        } else {
            list.push_back(atoi(token));
        }
        token = strtok(nullptr, ",");
    }
}

int get_id(unsigned long paddr, const std::vector<std::vector<int>>& functions) {
    int id = 0;
    for (size_t func_idx = 0; func_idx < functions.size(); func_idx++) {
        int bit_result = 0;
        for (int bit_pos : functions[func_idx]) {
            bit_result ^= ((paddr >> bit_pos) & 0x1);
        }
        if (bit_result) {
            id |= (1 << func_idx);
        }
    }
    return id;
}

int get_dram_bank(unsigned long paddr) {
    if (g_dram_functions.empty()) return -1;
    return get_id(paddr, g_dram_functions);
}

int get_llc_slice(unsigned long paddr) {
    if (g_llc_functions.empty()) return -1;
    return get_id(paddr, g_llc_functions);
}

// Legacy support for paddr_to_color if needed, or we can remove it.
// For now, I'll remove it as we are refactoring.


size_t get_frame_number_from_pagemap(size_t value) {
    return value & ((1ULL << 54) - 1);
}

ulong get_paddr(ulong vaddr)
{
	ulong value;
	int page_size = getpagesize();
	off64_t offset = (vaddr / page_size) * sizeof(value);
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

// Read XOR mapping functions from file
void read_xor_map_file(const char* filename, std::vector<std::vector<int>>& functions) {
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open map file %s\n", filename);
        exit(1);
    }

    char line[256];
    functions.clear();

    while (fgets(line, sizeof(line), fp)) {
        // Skip empty lines and comments
        if (line[0] == '\n' || line[0] == '#') continue;
        std::vector<int> function_bits;
        char* token = strtok(line, " \t\n");
        while (token != nullptr) {
            int bit = atoi(token);
            function_bits.push_back(bit);
            token = strtok(nullptr, " \t\n");
        }
        if (!function_bits.empty()) {
            functions.push_back(function_bits);
        }
    }
    fclose(fp);
    if (g_debug) {
        printf("Loaded %zu mapping functions from %s:\n", functions.size(), filename);
        for (size_t i = 0; i < functions.size(); i++) {
            printf("Function %zu: XOR bits ", i);
            for (int bit : functions[i]) {
                printf("%d ", bit);
            }
            printf("\n");
        }
    }
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

	int64_t *memchunk = NULL;
	int opt, prio;
	int i;

	long repeat = DEFAULT_ITER;
	int mlp = DEFAULT_MLP;
	struct timespec start, end;
	int acc_type = READ;

	std::srand (0);
	std::vector<int64_t> myvector;

	static struct option long_options[] = {
		{"dram_map", required_argument, 0, 1000},
		{"llc_map", required_argument, 0, 1001},
		{"dram_banks", required_argument, 0, 1002},
		{"llc_slices", required_argument, 0, 1003},
		{0, 0, 0, 0}
	};
	int option_index = 0;

	/*
	 * get command line options 
	 */
	while ((opt = getopt_long(argc, argv, "k:m:g:u:a:c:d:e:b:i:l:f:h", long_options, &option_index)) != -1) {
		switch (opt) {
		case 1000: // dram_map
			g_dram_map_file = optarg;
			break;
		case 1001: // llc_map
			g_llc_map_file = optarg;
			break;
		case 1002: // dram_banks
			parse_list(optarg, g_selected_dram_banks);
			break;
		case 1003: // llc_slices
			parse_list(optarg, g_selected_llc_slices);
			break;
		case 'k': /* set memory size in KB */
			g_mem_size = 1024 * strtol(optarg, NULL, 0);
			break;
		case 'm': /* set memory size in MB */
			g_mem_size = (1024*1024) * strtol(optarg, NULL, 0);
			break;
		case 'g': /* set memory size in GB */
			g_mem_size = (1024*1024*1024) * strtol(optarg, NULL, 0);
			break;
		case 'u': /* set unit size */
			g_unit_size = strtol(optarg, NULL, 0);
			if (g_unit_size % 4 != 0) {
				fprintf(stderr, "unit size must be multiple of 4\n");
				exit(1);
			}
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
		case 'f': /* bank map file */
			g_dram_map_file = optarg;
			fprintf(stderr, "Bank map file (mapped to dram_map): %s\n", g_dram_map_file);
			break;
		case 'h': /* help */
			printf("Usage: %s [options]\n", argv[0]);
			printf("Options:\n");
			printf("  -m <size>   : memory size in KB (default: %d)\n", DEFAULT_ALLOC_SIZE_KB);
			printf("  -g <size>   : memory size in GB\n");
			printf("  -u <size>   : unit size in bytes (default: %ld)\n", g_unit_size);
			printf("  -a <type>   : access type (read|write, default: read)\n");
			printf("  -b <mask>   : bank bitmask (default: 0x%lx)\n", bank_bitmask);
			printf("  -c <cpu>    : set CPU affinity (default: 0)\n");
			printf("  -d <debug>  : debug level (default: 0)\n");
			printf("  -e <color>  : select color (bank) for coloring\n");
			printf("  -f <file>   : bank bit mapping file\n");
			printf("  -p <prio>   : set process priority\n");
			printf("  -i <iter>   : number of iterations (default: %ld)\n", (long)DEFAULT_ITER);
			printf("  -l <mlp>    : memory-level parallelism (default: %d)\n", (int)DEFAULT_MLP);
            printf("  --dram_map <file> : DRAM XOR map file\n");
            printf("  --llc_map <file>  : LLC XOR map file\n");
            printf("  --dram_banks <list>: Selected DRAM banks (e.g. 0-3,5)\n");
            printf("  --llc_slices <list>: Selected LLC slices (e.g. 0,1)\n");
			exit(0);
		}

	}

	init_pagemap(); // need to open /proc/self/pagemap
	
	if (g_dram_map_file) {
		read_xor_map_file(g_dram_map_file, g_dram_functions);
	}
    if (g_llc_map_file) {
        read_xor_map_file(g_llc_map_file, g_llc_functions);
    }

    // Fallback to bitmask if no DRAM functions loaded
    if (g_dram_functions.empty()) {
        unsigned long c;
        for_each_set_bit(c, &bank_bitmask, BITS_PER_LONG) {
            std::vector<int> func;
            func.push_back((int)c);
            g_dram_functions.push_back(func);
        }
    }

    // Merge legacy colors into selected dram banks
    for (int i = 0; i < g_color_cnt; i++) {
        g_selected_dram_banks.push_back(g_color[i]);
    }
	
	printf("g_mem_size: %ld (%ld KB)\n", g_mem_size, g_mem_size/1024);
	printf("g_unit_size: %ld (%ld KB)\n", g_unit_size, g_unit_size/1024);
	printf("access type: %s\n", (acc_type == READ) ? "read" : "write");

	printf("\n");
    if (!g_selected_dram_banks.empty()) {
        printf("DRAM Mapping:\n");
        for (size_t i = 0; i < g_dram_functions.size(); i++) {
            printf("  Function %zu: XOR bits ", i);
            for (int bit : g_dram_functions[i]) printf("%d ", bit);
            printf("\n");
        }
        printf("Selected DRAM banks: ");
        for (int b : g_selected_dram_banks) printf("%d ", b);
        printf("\n");
    }

    if (!g_selected_llc_slices.empty()) {
        printf("LLC Mapping:\n");
        for (size_t i = 0; i < g_llc_functions.size(); i++) {
            printf("  Function %zu: XOR bits ", i);
            for (int bit : g_llc_functions[i]) printf("%d ", bit);
            printf("\n");
        }
        printf("Selected LLC slices: ");
        for (int s : g_selected_llc_slices) printf("%d ", s);
        printf("\n");
    }
	
	srand(0);

	int64_t ws = 0;
	int64_t orig_ws = (g_mem_size / g_unit_size);

	printf("orig_ws: %ld  mlp: %d\n", orig_ws, mlp);

	clock_gettime(CLOCK_REALTIME, &start);

	/* alloc memory. align to a page boundary */
    // try 1GB huge page
    memchunk = (int64_t *)mmap(NULL, g_mem_size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE |
                    (30 << MAP_HUGE_SHIFT), -1, 0);
    if ((void *)memchunk == MAP_FAILED) {
        // try 2MB huge page
        memchunk = (int64_t *)mmap(NULL, g_mem_size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE,
                        -1, 0);
        if ((void *)memchunk == MAP_FAILED) {
            // nomal page allocation
            memchunk = (int64_t *)mmap(NULL, g_mem_size, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
            if ((void *)memchunk == MAP_FAILED) {
                perror("alloc failed");
                exit(1);
            } else
                printf("small page mapping (%u KB)\n", getpagesize() / 1024);
        } else
            printf("%s huge page mapping\n", "2MB");
    } else {
        printf("%s huge page mapping\n", "1GB");
	}

	/* initialize data */
	memset(memchunk, 0, g_mem_size);

	// set some values:
	for (int i=0; i<orig_ws; i++) {
		ulong vaddr = (ulong)&memchunk[i*g_unit_size/8];
        bool selected = true;

        if (!g_selected_dram_banks.empty() || !g_selected_llc_slices.empty()) {
            ulong paddr = get_paddr(vaddr);

            if (!g_selected_dram_banks.empty()) {
                int bank = get_dram_bank(paddr);
                bool found = false;
                for (int b : g_selected_dram_banks) {
                    if (b == bank) { found = true; break; }
                }
                if (!found) selected = false;
            }

            if (selected && !g_selected_llc_slices.empty()) {
                int slice = get_llc_slice(paddr);
                bool found = false;
                for (int s : g_selected_llc_slices) {
                    if (s == slice) { found = true; break; }
                }
                if (!found) selected = false;
            }
            
            if (selected && g_debug) {
                 printf("vaddr: %p paddr: %p dram: %d llc: %d\n",
                       (void *)vaddr, (void *)paddr, 
                       get_dram_bank(paddr), get_llc_slice(paddr));
            }
        }

        if (selected) {
            myvector.push_back(i);
        }
	}

	// using built-in random generator:
	std::shuffle(myvector.begin(), myvector.end(), std::default_random_engine(0));

	// update the workingset size
	ws = myvector.size() / mlp * mlp; 
	printf("new ws: %ld\n", ws);
	int64_t list_len = ws / mlp;
	printf("list_len: %ld\n", list_len);
	
	for (i = 0; i < ws; i++) {
		int64_t l = i / list_len;
		int64_t curr_idx = myvector[i] * g_unit_size / 8;
		int64_t next_idx = myvector[i+1] * g_unit_size / 8;
		if ((i+1) % list_len == 0)
			next_idx = myvector[i/list_len*list_len] * g_unit_size / 8;

		memchunk[curr_idx] = next_idx;
		
		if (i % list_len == 0) {
			list[l] = memchunk;
			next[l] = curr_idx;
			printf("list[%ld]  %ld\n", l,  next[l] * 8 / g_unit_size);
		}
		
		// printf("%8d ->%8d\n", myvector[i], next_idx*4/g_unit_size);
	}
	
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

	printf("alloc. size: %ld (%ld KB)\n", g_mem_size, g_mem_size/1024);
	int64_t total_ws =  ws * g_unit_size;
	printf("ws size: %ld (%ld KB)\n", total_ws, total_ws / 1024);
	printf("duration %.0f ns, #access %ld\n", (double)nsdiff, naccess);
	printf("Avg. latency %.2f ns\n", avglat);	
	printf("bandwidth %.2f MB/s\n", (double)64*1000*naccess/nsdiff);

	return 0;
}
