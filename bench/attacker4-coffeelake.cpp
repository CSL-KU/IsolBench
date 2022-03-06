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

#include "cl2.hpp"

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
enum access_type { READ, WRITE};

/**************************************************************************
 * Global Variables
 **************************************************************************/
static int g_mem_size = (DEFAULT_ALLOC_SIZE_KB*1024);
static int* list[MAX_MLP];
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

long run_write(long iter, int mlp)
{
	long cnt = 0;

	for (long i = 0; i < iter; i++) {
		switch (mlp) {
		case 32:
			list[31][next[31]+1] = 0xff;
			next[31] = list[31][next[31]];
		case 31:
			list[30][next[30]+1] = 0xff;
			next[30] = list[30][next[30]];
		case 30:
			list[29][next[29]+1] = 0xff;
			next[29] = list[29][next[29]];
		case 29:
			list[28][next[28]+1] = 0xff;
			next[28] = list[28][next[28]];
		case 28:
			list[27][next[27]+1] = 0xff;
			next[27] = list[27][next[27]];
		case 27:
			list[26][next[26]+1] = 0xff;
			next[26] = list[26][next[26]];
		case 26:
			list[25][next[25]+1] = 0xff;
			next[25] = list[25][next[25]];
		case 25:
			list[24][next[24]+1] = 0xff;
			next[24] = list[24][next[24]];
		case 24:
			list[23][next[23]+1] = 0xff;
			next[23] = list[23][next[23]];
		case 23:
			list[22][next[22]+1] = 0xff;
			next[22] = list[22][next[22]];
		case 22:
			list[21][next[21]+1] = 0xff;
			next[21] = list[21][next[21]];
		case 21:
			list[20][next[20]+1] = 0xff;
			next[20] = list[20][next[20]];
		case 20:
			list[19][next[19]+1] = 0xff;
			next[19] = list[19][next[19]];
		case 19:
			list[18][next[18]+1] = 0xff;
			next[18] = list[18][next[18]];
		case 18:
			list[17][next[17]+1] = 0xff;
			next[17] = list[17][next[17]];
		case 17:
			list[16][next[16]+1] = 0xff;
			next[16] = list[16][next[16]];
		case 16:
			list[15][next[15]+1] = 0xff;
			next[15] = list[15][next[15]];
		case 15:
			list[14][next[14]+1] = 0xff;
			next[14] = list[14][next[14]];
		case 14:
			list[13][next[13]+1] = 0xff;
			next[13] = list[13][next[13]];
		case 13:
			list[12][next[12]+1] = 0xff;
			next[12] = list[12][next[12]];
		case 12:
			list[11][next[11]+1] = 0xff;
			next[11] = list[11][next[11]];
		case 11:
			list[10][next[10]+1] = 0xff;
			next[10] = list[10][next[10]];
		case 10:
			list[9][next[9]+1] = 0xff;
			next[9] = list[9][next[9]];
		case 9:
			list[8][next[8]+1] = 0xff;
			next[8] = list[8][next[8]];
		case 8:
			list[7][next[7]+1] = 0xff;
			next[7] = list[7][next[7]];
		case 7:
			list[6][next[6]+1] = 0xff;
			next[6] = list[6][next[6]];
		case 6:
			list[5][next[5]+1] = 0xff;
			next[5] = list[5][next[5]];
		case 5:
			list[4][next[4]+1] = 0xff;
			next[4] = list[4][next[4]];
		case 4:
			list[3][next[3]+1] = 0xff;
			next[3] = list[3][next[3]];
		case 3:
			list[2][next[2]+1] = 0xff;
			next[2] = list[2][next[2]];
		case 2:
			list[1][next[1]+1] = 0xff;
			next[1] = list[1][next[1]];
		case 1:
			list[0][next[0]+1] = 0xff;
			next[0] = list[0][next[0]];
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
	int mlp = 1;
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
                case 'x':
			use_hugepage = (use_hugepage) ? 0: 1;
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

	/* alloc memory. align to a page boundary */
#if 0
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
#else

	// Get platform and device information
	cl_platform_id platform_id = NULL;
	cl_device_id device_id = NULL;   
	cl_uint ret_num_devices;
	cl_uint ret_num_platforms;
	cl_device_svm_capabilities caps;
	cl_int ret, err;

	err = clGetPlatformIDs(1, &platform_id, &ret_num_platforms);
	if (ret_num_platforms == 0) {
		std::cerr<<" No platforms found. Check OpenCL installation!\n";
		exit(1);
	}

	err = clGetDeviceIDs(platform_id, CL_DEVICE_TYPE_DEFAULT, 1, 
			     &device_id, &ret_num_devices);
	if (ret_num_devices == 0) {
		std::cerr<<" No devices found. Check OpenCL installation!\n";
		exit(1);
	}

	// Create an OpenCL context
	cl_context context = clCreateContext( NULL, 1, &device_id, NULL, NULL, &ret);

	// print device name
	size_t valueSize;
	clGetDeviceInfo(device_id, CL_DEVICE_NAME, 0, NULL, &valueSize);
	char *value = (char*) malloc(valueSize);
	clGetDeviceInfo(device_id, CL_DEVICE_NAME, valueSize, value, NULL);
	printf("%d. Device: %s\n", 1, value);
	free(value);

	// print hardware device version
	clGetDeviceInfo(device_id, CL_DEVICE_VERSION, 0, NULL, &valueSize);
	value = (char*) malloc(valueSize);
	clGetDeviceInfo(device_id, CL_DEVICE_VERSION, valueSize, value, NULL);
	printf(" %d.%d Hardware version: %s\n", 1, 1, value);
	free(value);

	// print software driver version
	clGetDeviceInfo(device_id, CL_DRIVER_VERSION, 0, NULL, &valueSize);
	value = (char*) malloc(valueSize);
	clGetDeviceInfo(device_id, CL_DRIVER_VERSION, valueSize, value, NULL);
	printf(" %d.%d Software version: %s\n", 1, 2, value);
	free(value);

	// print c version supported by compiler for device
	clGetDeviceInfo(device_id, CL_DEVICE_OPENCL_C_VERSION, 0, NULL, &valueSize);
	value = (char*) malloc(valueSize);
	clGetDeviceInfo(device_id, CL_DEVICE_OPENCL_C_VERSION, valueSize, value, NULL);
	printf(" %d.%d OpenCL C version: %s\n", 1, 3, value);
	free(value);

	// print parallel compute units
	cl_uint maxComputeUnits;
	clGetDeviceInfo(device_id, CL_DEVICE_MAX_COMPUTE_UNITS,
			sizeof(maxComputeUnits), &maxComputeUnits, NULL);
	printf(" %d.%d Parallel compute units: %d\n", 1, 4, maxComputeUnits);

	err = clGetDeviceInfo(device_id,
				     CL_DEVICE_SVM_CAPABILITIES,
				     sizeof(cl_device_svm_capabilities),
				     &caps,
				     0);
	printf(" 1.4 SVM support:");
	if (caps & CL_DEVICE_SVM_COARSE_GRAIN_BUFFER)
		printf(" CL_DEVICE_SVM_COARSE_GRAIN");
	if (caps & CL_DEVICE_SVM_FINE_GRAIN_BUFFER)
		printf(" CL_DEVICE_SVM_FINE_GRAIN_BUFFER");
	if (caps & CL_DEVICE_SVM_FINE_GRAIN_SYSTEM)
		printf(" CL_DEVICE_SVM_FINE_GRAIN_SYSTEM");
	if (caps & CL_DEVICE_SVM_ATOMICS)
		printf(" CL_DEVICE_SVM_ATOMICS");
	printf("\n");
	
	// Create a command queue
	cl_command_queue command_queue = clCreateCommandQueueWithProperties(context, device_id, 0, &ret);


	memchunk = (int *)clSVMAlloc(context, CL_MEM_READ_WRITE, g_mem_size, 0);
	next = (int *)clSVMAlloc(context, CL_MEM_READ_WRITE, MAX_MLP * sizeof(int), 0);
#endif
	
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

#if 1
		if ((i+1) % list_len == 0)
			next_idx = myvector[i/list_len*list_len] * CACHE_LINE_SIZE / 4;
#else
		if (i + 1 == ws)
			next_idx = myvector[0] * CACHE_LINE_SIZE / 4;
#endif		
		
		memchunk[curr_idx] = next_idx;
		
		if (i % list_len == 0) {
			list[l] = memchunk;
			next[l] = curr_idx;
			printf("next[%d]  %d\n", l,  next[l]);
		}
		
		// printf("%8d ->%8d\n", myvector[i], next_idx*4/CACHE_LINE_SIZE);
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
	int64_t nsdiff;
	double  avglat;
	int total_ws;
#if 0
	clock_gettime(CLOCK_REALTIME, &start);	
	/* actual access */
	if (acc_type == READ)
		naccess = run((int64_t)repeat * list_len, mlp);
	else
		naccess = run_write((int64_t)repeat * list_len, mlp);
	clock_gettime(CLOCK_REALTIME, &end);

	nsdiff = get_elapsed(&start, &end);
	avglat = (double)nsdiff/naccess;

	printf("alloc. size: %d (%d KB)\n", g_mem_size, g_mem_size/1024);
	total_ws =  ws * CACHE_LINE_SIZE;
	printf("ws size: %d (%d KB)\n", total_ws, total_ws / 1024);
	printf("list_len: %d (%d KB)\n", list_len, list_len * CACHE_LINE_SIZE / 1024);
	printf("mlp: %d\n", mlp);
	printf("duration %.0f ns, #access %ld\n", (double)nsdiff, naccess);
	printf("Avg. latency %.2f ns\n", avglat);	
	printf("bandwidth %.2f MB/s\n", (double)64*1000*naccess/nsdiff);
#endif


	// Load the kernel source code into the array source_str
	FILE *fp;
	char *source_str;
	size_t source_size;
	
	fp = fopen("gpuwrite.cl", "r");
	if (!fp) {
		fprintf(stderr, "Failed to load kernel.\n");
		exit(1);
	}
	#define MAX_SOURCE_SIZE (0x100000)
	source_str = (char*)malloc(MAX_SOURCE_SIZE);
	source_size = fread( source_str, 1, MAX_SOURCE_SIZE, fp);
	fclose( fp );
 	
	cl_program program = clCreateProgramWithSource(context, 1,
						       (const char **)&source_str, (const size_t *)&source_size, &ret); 
	ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL);
	cl_kernel kernel = clCreateKernel(program, "gpuwrite", &ret);
	clSetKernelArgSVMPointer(kernel, 0, memchunk);
	clSetKernelArgSVMPointer(kernel, 1, next);
	int iter = repeat * list_len; 
	clSetKernelArg(kernel, 2, sizeof(int), &iter);
	
	size_t global_item_size = mlp;
	size_t local_item_size = 1;

	clock_gettime(CLOCK_REALTIME, &start);
	
	clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, &global_item_size, NULL, 0, NULL, NULL);
	ret = clFlush(command_queue);
	ret = clFinish(command_queue);
	
	clock_gettime(CLOCK_REALTIME, &end);


	ret = clReleaseKernel(kernel);
	ret = clReleaseProgram(program);

	naccess = global_item_size * list_len * repeat;


	nsdiff = get_elapsed(&start, &end);
	avglat = (double)nsdiff/naccess;

	printf("alloc. size: %d (%d KB)\n", g_mem_size, g_mem_size/1024);
	total_ws =  ws * CACHE_LINE_SIZE;
	printf("ws size: %d (%d KB)\n", total_ws, total_ws / 1024);
	printf("list_len: %d (%d KB)\n", list_len, list_len * CACHE_LINE_SIZE / 1024);
	printf("mlp: %d\n", mlp);
	printf("duration %.0f ns, #access %ld\n", (double)nsdiff, naccess);
	printf("Avg. latency %.2f ns\n", avglat);	
	printf("bandwidth %.2f MB/s\n", (double)64*1000*naccess/nsdiff);
	
	return 0;
}
