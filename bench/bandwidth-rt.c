/**
 *
 * Copyright (C) 2019  Heechul Yun <heechul.yun@ku.edu>
 *
 * Periodic real-time task related functions are from:
 *    http://www.2net.co.uk/tutorial/periodic_threads
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
#include <getopt.h>
#include <time.h>
#include <pthread.h>

/**************************************************************************
 * Public Definitions
 **************************************************************************/
#define CACHE_LINE_SIZE 64	   /* cache Line size is 64 byte */
#ifdef __arm__
#  define DEFAULT_ALLOC_SIZE_KB 4096
#else
#  define DEFAULT_ALLOC_SIZE_KB 16384
#endif

#define MAX(a,b) ((a>b)?(a):(b))
#define MIN(a,b) ((a>b)?(b):(a))

/**************************************************************************
 * Public Types
 **************************************************************************/
enum access_type { READ, WRITE};

struct periodic_info
{
	int id;
	/* Opaque data */
	int sig;
	timer_t timer_id;	
	sigset_t alarm_sig;
	int wakeups_missed;	
};

/**************************************************************************
 * Global Variables
 **************************************************************************/
int g_mem_size = DEFAULT_ALLOC_SIZE_KB * 1024;	   /* memory size */
char *g_mem_ptr = 0;		   /* pointer to allocated memory region */

int g_nthreads = 1;
volatile int g_njoin = 0;
int acc_type = READ;
int iterations = 0;
int jobs = 0;
int period = 0; /* in ms */
int verbose = 0;
int cpuid = 0;
int thread_local = 0;

volatile uint64_t g_nread = 0;	           /* number of bytes read */
volatile unsigned int g_start;		   /* starting time */

/**************************************************************************
 * Public Functions
 **************************************************************************/
unsigned int get_usecs()
{
	struct timeval         time;
	gettimeofday(&time, NULL);
	return (time.tv_sec * 1000000 +	time.tv_usec);
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

int64_t bench_read(char *mem_ptr)
{
	int i;	
	int64_t sum = 0;
	for ( i = 0; i < g_mem_size; i+=(CACHE_LINE_SIZE) ) {
		sum += mem_ptr[i];
	}
	// g_nread += g_mem_size;	
	__atomic_fetch_add(&g_nread, g_mem_size, __ATOMIC_SEQ_CST);
	return sum;
}

int bench_write(char *mem_ptr)
{
	register int i;
	for ( i = 0; i < g_mem_size; i+=(CACHE_LINE_SIZE) ) {
		mem_ptr[i] = 0xff;
	}
	// g_nread += g_mem_size;	
	__atomic_fetch_add(&g_nread, g_mem_size, __ATOMIC_SEQ_CST);	
	return 1;
}

int make_periodic (int unsigned period, struct periodic_info *info)
{
	static int next_sig;
	int ret;
	unsigned int ns;
	unsigned int sec;
	struct sigevent sigev;

	struct itimerspec itval;

	/* Initialise next_sig first time through. We can't use static
	   initialisation because SIGRTMIN is a function call, not a constant */
	if (next_sig == 0)
		next_sig = SIGRTMIN;
	/* Check that we have not run out of signals */
	if (next_sig > SIGRTMAX)
		return -1;
	info->sig = next_sig;
	next_sig++;

	info->wakeups_missed = 0;

	/* Create the signal mask that will be used in wait_period */
	sigemptyset (&(info->alarm_sig));
	sigaddset (&(info->alarm_sig), info->sig);

	/* Create a timer that will generate the signal we have chosen */
	sigev.sigev_notify = SIGEV_SIGNAL;
	sigev.sigev_signo = info->sig;
	sigev.sigev_value.sival_ptr = (void *) &info->timer_id;
	ret = timer_create (CLOCK_MONOTONIC, &sigev, &info->timer_id);
	if (ret == -1)
		return ret;

	/* Make the timer periodic */
	sec = period/1000000;
	ns = (period - (sec * 1000000)) * 1000;
	itval.it_interval.tv_sec = sec;
	itval.it_interval.tv_nsec = ns;
	itval.it_value.tv_sec = sec;
	itval.it_value.tv_nsec = ns;
	ret = timer_settime (info->timer_id, 0, &itval, NULL);
	return ret;
}

void wait_period (struct periodic_info *info)
{
	int sig;
	sigwait (&(info->alarm_sig), &sig);
        info->wakeups_missed += timer_getoverrun (info->timer_id);
}

void worker(void *param)
{
	int64_t sum = 0;
	int i,j;
	char *l_mem_ptr;
	
	struct periodic_info *info = (struct periodic_info *)param;

	/*
	 * allocate contiguous region of memory 
	 */
	if (thread_local) {
		l_mem_ptr = malloc(g_mem_size);
		memset(l_mem_ptr, 1, g_mem_size);
	} else {
		l_mem_ptr = g_mem_ptr;
	}

	__atomic_fetch_add(&g_njoin, 1, __ATOMIC_SEQ_CST);
	while (g_njoin < g_nthreads); // busy wait until all join

	/*
	 * actual memory access
	 */
	if (period > 0) make_periodic(period * 1000, info);
	for (j = 0;; j++) {
		unsigned int l_start, l_end, l_duration;
		l_start = get_usecs();
		for (i = 0;; i++) {
			switch (acc_type) {
			case READ:
				sum += bench_read(l_mem_ptr);
				break;
			case WRITE:
				sum += bench_write(l_mem_ptr);
				break;
			}
			if (verbose > 1) fprintf(stderr, ".");
			if (iterations > 0 && i+1 >= iterations)
				break;
		}
		l_end = get_usecs();
		l_duration = l_end - l_start;
		if (period > 0) wait_period (info);
		if (verbose) fprintf(stderr, "\nJob %d Took %d us", j, l_duration);
		if (jobs == 0 || j+1 >= jobs)
			break;
	}
	if (verbose) {
		printf("thread %d completed: wakeup misses=%d\n",
		       info->id, info->wakeups_missed);
	}

	printf("\ntotal sum = %" PRId64 "\n", sum);
}
	
void usage(int argc, char *argv[])
{
	printf("Usage: $ %s [<option>]*\n\n", argv[0]);
	printf("-m: memory size in KB. deafult=8192\n");
	printf("-a: access type - read, write. default=read\n");
	printf("-n: number of threads. default=1 \n");
	printf("-t: time to run in sec. 0 means indefinite. default=5. \n");
	printf("-c: CPU to run.\n");
	printf("-r: real-time priority (use SCHED_FIFO).\n");
	printf("-p: nice value (use CFS).\n");
	printf("-i: iterations. 0 means intefinite. default=0\n");
	printf("-j: jobs. default=0\n");
	printf("-l: job period (in ms)\n");
	printf("-v: debug level (in ms)\n");
	printf("-o: per-thread allocation\n");
	printf("-h: help\n");
	printf("\nExamples: \n$ bandwidth-rt -m 8192 -c 2 -a read -i 10 -j 100 -l 10 -c 2\n  <- 8MB read*10 iterations per job, for 100 jobs with 10ms period, on CPU 2\n");
	exit(1);
}

	
int main(int argc, char *argv[])
{
	unsigned finish = 5;
	int prio = 0;        
	int num_processors;
	int opt;
	cpu_set_t cmask;
	int i;
	struct sched_param param;
	sigset_t alarm_sig;
	pthread_t tid[32]; /* thread identifier */
	struct periodic_info info[32];
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	
	static struct option long_options[] = {
		{"threads", required_argument, 0,  'n' },		
		{"period",  required_argument, 0,  'l' },
		{"jobs",    required_argument, 0,  'j' },
		{"verbose", required_argument, 0,  'v' },
		{"local",   no_argument,       0,  'o' },
		{0,         0,                 0,  0 }
	};
	int option_index = 0;

	num_processors = sysconf(_SC_NPROCESSORS_CONF);
	
	/*
	 * get command line options 
	 */
	while ((opt = getopt_long(argc, argv, "m:n:a:t:c:r:p:i:j:l:hv:o",
				  long_options, &option_index)) != -1) {
		switch (opt) {
		case 'm': /* set memory size */
			g_mem_size = 1024 * strtol(optarg, NULL, 0);
			break;
		case 'n': /* #of threads */
			g_nthreads = strtol(optarg, NULL, 0);
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
			
		case 'c': /* set CPU affinity */
			cpuid = strtol(optarg, NULL, 0);
			break;

		case 'r': /* set rt scheduler and its priority */
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
		case 'i': /* iterations of a job */
			iterations = strtol(optarg, NULL, 0);
			break;
		case 'j': /* #of jobs */
			jobs = strtol(optarg, NULL, 0);
			break;
		case 'l': /* period -> determine P (ms)*/
			period = strtol(optarg, NULL, 0);
			sigemptyset (&alarm_sig);
			for (i = SIGRTMIN; i <= SIGRTMAX; i++)
				sigaddset (&alarm_sig, i);
			sigprocmask (SIG_BLOCK, &alarm_sig, NULL);
			break;
		case 'h': 
			usage(argc, argv);
			break;
		case 'v':
			verbose = strtol(optarg, NULL, 0);
			break;
		case 'o':
			thread_local = 1;
			break;
		}
	}

	/*
	 * allocate contiguous region of memory 
	 */
	g_mem_ptr = malloc(g_mem_size);
	memset(g_mem_ptr, 1, g_mem_size);

	/* print experiment info before starting */
	printf("mem=%d KB (%s), type=%s, nthreads=%d cpuid=%d, iterations=%d, jobs=%d, period=%d\n",
	       g_mem_size/1024,
	       (thread_local)? "private":"shared",
	       ((acc_type==READ) ?"read": "write"),
	       g_nthreads,
	       cpuid,
	       iterations,
               jobs,
	       period);
	printf("stop at %d\n", finish);

	/* set signals to terminate once time has been reached */
	signal(SIGINT, &quit);
	signal(SIGTERM, &quit);
	signal(SIGHUP, &quit); 
	if (finish > 0) {
		signal(SIGALRM, &quit);
		alarm(finish);
	}

	g_start = get_usecs();
	
	/* thread affinity set */
	for (i = 0; i < MIN(g_nthreads, num_processors); i++) {
		pthread_create(&tid[i], &attr, (void *)worker, &info[i]);
		CPU_ZERO(&cmask);
		CPU_SET((cpuid + i) % num_processors, &cmask);
		if (pthread_setaffinity_np(tid[i], sizeof(cpu_set_t), &cmask) < 0)
			perror("error");
	}

	for (i = 0; i < MIN(g_nthreads, num_processors); i++) {
		pthread_join(tid[i], NULL);
		printf("thread %d finished\n", i);
	}

	quit(0);

	return 0;
}

