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
#ifndef _GNU_SOURCE
#  define _GNU_SOURCE             /* See feature_test_macros(7) */
#endif

/**************************************************************************
 * Included Files
 **************************************************************************/
#include <iostream>     // std::cout
#include <algorithm>    // std::random_shuffle
#include <vector>       // std::vector
#include <ctime>        // std::time
#include <cstdlib>      // std::rand, std::srand

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
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>
#include <limits.h>

#define CL_HPP_MINIMUM_OPENCL_VERSION 120
#define CL_HPP_TARGET_OPENCL_VERSION 120
#include "cl2.hpp"

#include <vector>
#include <string>
#include <sstream>

/**************************************************************************
 * Public Definitions
 **************************************************************************/
#define MAX_MLP 256
#define CACHE_LINE_SIZE 64	   /* cache Line size is 64 byte */
#define DEFAULT_ALLOC_SIZE_KB 16384
#define DEFAULT_ITER 1000

#ifndef MAX_SOURCE_SIZE
#define MAX_SOURCE_SIZE (0x100000)
#endif
#define MAX_COLORS 64
#define startScalar (1.0)

/**************************************************************************
 * Public Types
 **************************************************************************/

/**************************************************************************
 * Global Variables
 **************************************************************************/
size_t g_mem_size = DEFAULT_ALLOC_SIZE_KB * 1024;	   /* memory size */

int *g_mem_ptr = 0;		   /* pointer to allocated memory region */
int next[MAX_MLP];

static int g_debug = 0;
static int g_color[MAX_COLORS]; // not assigned
static int g_color_cnt = 0;

volatile uint64_t g_nread = 0;	           /* number of bytes read */



/**************************************************************************
 * Public Functions
 **************************************************************************/
uint64_t get_elapsed(struct timespec *start, struct timespec *end)
{
	uint64_t dur;

	dur = ((uint64_t)end->tv_sec * 1000000000 + end->tv_nsec) - 
		((uint64_t)start->tv_sec * 1000000000 + start->tv_nsec);
	return dur;
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


void usage(int argc, char *argv[])
{
	printf("Usage: $ %s [<option>]*\n\n", argv[0]);
	printf("-m: memory size in KB. deafult=8192\n");
	printf("-c: CPU to run.\n");
	printf("-e: color\n");
	printf("-h: help\n");
	printf("\nExamples: \n$ gpuwrite -m 8192 -e 0 \n  <- 8MB write, color = 0\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	// struct sched_param param;
        cpu_set_t cmask;
	int num_processors;
	int cpuid = 0;

	int *memchunk = NULL;
	int opt, prio;
	int i;

	long repeat = DEFAULT_ITER;
	int use_hugepage = 0;
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
                case 'x':
			use_hugepage = (use_hugepage) ? 0: 1;
			break;
		case 'h':
			usage(argc, argv);
			exit(1);
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
	
	int ws = 0;
	int orig_ws = (g_mem_size / CACHE_LINE_SIZE);

	printf("orig_ws: %d  mlp: %d\n", orig_ws, MAX_MLP);

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
		ulong vaddr = (ulong)&memchunk[i*CACHE_LINE_SIZE/4];

		if (g_color_cnt > 0) {
			/* use coloring */
			for (int j = 0; j < g_color_cnt; j++) {
				if (paddr_to_color(vaddr) == g_color[j]) {
					if (g_debug)
						printf("vaddr: %p color: %d\n",
						       (void *)vaddr,
						       paddr_to_color(vaddr));
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
	ws = myvector.size() / MAX_MLP * MAX_MLP;
	printf("new ws: %d\n", ws);
	int list_len = ws / MAX_MLP;
	printf("list_len: %d\n", list_len);

	printf("new ws: %d\n", ws);
	for (i = 0; i < ws; i++) {
		int l = i / list_len;
		int curr_idx = myvector[i] * CACHE_LINE_SIZE / 4;
		int next_idx = myvector[i+1] * CACHE_LINE_SIZE / 4;
		if ((i+1) % list_len == 0)
			next_idx = myvector[i/list_len*list_len] * CACHE_LINE_SIZE / 4;
		memchunk[curr_idx] = next_idx;
		if (i % list_len == 0) {
			next[l] = curr_idx;
			printf("list[%d]  %d\n", l,  next[l] * 4 / CACHE_LINE_SIZE);
		}
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

	// GPU code
	cl::Device device;
	cl::Context context;
	cl::CommandQueue queue;
	cl::Buffer cl_mem_ptr;
	cl::Buffer cl_next_ptr;

// clEnqueueMapBuffer (map device buffer on the host address space.
// https://www.khronos.org/registry/OpenCL/sdk/1.0/docs/man/xhtml/clEnqueueMapBuffer.html
// [...]
// bufferOut = clCreateBuffer(context, CL_MEM_ALLOC_HOST_PTR, imageW * imageH * sizeof (cl_int), NULL, &errCode);
// ciErr = clEnqueueNDRangeKernel(clComQueue, clKernel, 2, NULL, szGlobalWorkSize, szLocalWorkSize, 0, NULL, NULL);
// imageOut = clEnqueueMapBuffer(queue, bufferOut, CL_TRUE, CL_MAP_READ, 0, szOut, 0, NULL, NULL, &errCode);
// here you can read the data from imageOut
// errCode = clEnqueueUnmapMemObject(queue, bufferOut, imageOut, 0, NULL, NULL);


	std::string kernels{R"CLC(
  constant TYPE scalar = startScalar;
  kernel void memwrite(global int* a)
  {
    // const size_t sz = get_global_size(0);
    const size_t i = get_global_id(0);
    a[i<<20] = i;

    // size_t col = i % (1<<20);
    // size_t row = i >> 20;
    // a[(col << 20) | row] = (float)i;
    // if (i == 0) printf("sz=%u sz2=%u\n", sz, get_global_size(1));
  }
  
  kernel void write(global int* array, global int *next)
  {
    const size_t tid = get_global_id(0);
    int iter = 1000;
    if (tid % 32 == 0) printf("next[%u]=%u\n", tid, next[tid]);
    for (int r = 0; r < iter; r++) {
      array[next[tid]+1] = 0xff;
      next[tid] = array[next[tid]];
    }
  }
)CLC"};
	
	// Get GPU Device
	std::vector<cl::Device> devices;
	std::vector<cl::Platform> platforms;
	cl::Platform::get(&platforms);
	if (platforms.size() == 0) {
		std::cout<<" No platforms found. Check OpenCL installation!\n";
		exit(1);
	}
	for (unsigned i = 0; i < platforms.size(); i++)
	{
		std::vector<cl::Device> plat_devices;
		platforms[i].getDevices(CL_DEVICE_TYPE_ALL, &plat_devices);
		devices.insert(devices.end(), plat_devices.begin(), plat_devices.end());
	}
	if (devices.size() == 0) {
		std::cout<<" No devices found. Check OpenCL installation!\n";
		exit(1);
	}
	device = devices[0];
	std::cout<< "Using device: "<<device.getInfo<CL_DEVICE_NAME>()<<"\n";
	
	// Create GPU kernel
	context = cl::Context(device);
	queue = cl::CommandQueue(context);
	cl::Program program(context, kernels);
	std::ostringstream args;
	args << "-DstartScalar=" << startScalar << " ";
	args << "-DTYPE=int";
	if (program.build(args.str().c_str()) != CL_SUCCESS) {
		std::cout<<" Error building: "<<program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device)<<"\n";
		exit(1);
	}
	
	size_t alloc_sz = g_mem_size;
	size_t elem = MAX_MLP;

	// create buffer on the device
	cl_mem_ptr = cl::Buffer(context, CL_MEM_READ_WRITE, alloc_sz);
	cl_next_ptr = cl::Buffer(context, CL_MEM_READ_WRITE, MAX_MLP * sizeof(int));

	// write array g_mem_ptr to the device
	queue.enqueueWriteBuffer(cl_mem_ptr, CL_TRUE, 0, alloc_sz, g_mem_ptr);
	queue.enqueueWriteBuffer(cl_next_ptr, CL_TRUE, 0, MAX_MLP * sizeof(int), next);
	// queue.enqueueMapBuffer(cl_mem_ptr, CL_TRUE, CL_MAP_READ | CL_MAP_WRITE, 0, INDIVIDUALS_SIZE);

#if 0
	cl::KernelFunctor<cl::Buffer> *write_kernel;
	write_kernel = new cl::KernelFunctor<cl::Buffer>(program, "write");

	clock_gettime(CLOCK_REALTIME, &start);
	(*write_kernel)(cl::EnqueueArgs(queue, cl::NullRange, cl::NDRange(elem)), cl_mem_ptr, cl_next_ptr);
	queue.finish();
	clock_gettime(CLOCK_REALTIME, &end);
#else
	cl::Kernel write_kernel = cl::Kernel(program, "write");
	write_kernel.setArg(0, cl_mem_ptr);
	write_kernel.setArg(1, cl_next_ptr);

	clock_gettime(CLOCK_REALTIME, &start);
	queue.enqueueNDRangeKernel(write_kernel, cl::NullRange, cl::NDRange(elem));
	queue.finish();
	clock_gettime(CLOCK_REALTIME, &end);
#endif

	g_nread += elem * CACHE_LINE_SIZE * repeat;

	int64_t nsdiff = get_elapsed(&start, &end);
	printf("alloc. size: %d (%d KB)\n", (int)g_mem_size, (int)g_mem_size/1024);
	int total_ws =  ws * CACHE_LINE_SIZE;
	printf("ws size: %d (%d KB)\n", total_ws, total_ws / 1024);
	printf("duration %.0f ns\n", (double)nsdiff);
	printf("bandwidth %.2f MB/s\n", (double)g_nread*1000/nsdiff);
	
	return 0;
}
