/* Householder Kernel */
#include <stdint.h>
#include <stddef.h>
#include <math.h>

#include "../commlib.h"
#include "../shared.h"

/* global declarations */
static void kernel(void);

#define COMM_HEAPSIZE 3300

/* === Epiphany kernel boilerplate ===================================== */
#ifdef COMM_EPIPHANY
	#include <e-lib.h>

	/* globals */
	volatile shm_t shm SECTION(".shared_dram");
	uint32_t core;

	#define BARRIER do { e_barrier(barriers, tgt_bars); } while(0);
	volatile e_barrier_t barriers[CORES];
	         e_barrier_t *tgt_bars[CORES];

	/* commlib heap */
	char commlib_heap[COMM_HEAPSIZE];

	/* entry point */
	int main()
	{
		/* initialization */
		unsigned row, col;
		e_coords_from_coreid(e_get_coreid(), &row, &col);
		core = row * CORES_X + col;
		e_barrier_init(barriers, tgt_bars);

		/* run kernel */
		kernel();

		/* done */
		while(1) {
			__asm__ volatile("IDLE");
		}
	}
#endif

/* === Pthreads kernel boilerplate ===================================== */
#ifdef COMM_PTHREAD
	#include <pthread.h>
	#include <stdio.h>

	/* globals */
	extern shm_t shm;
	__thread uint32_t core;

	/* barrier magic */
	#define BARRIER do { pthread_barrier_wait(&barrier); } while(0);
	pthread_barrier_t barrier;
	void __attribute__((constructor)) barrier_constructor()
		{ pthread_barrier_init(&barrier, NULL, CORES); }

	/* commlib heap */
	__thread char commlib_heap[COMM_HEAPSIZE];

	/* entry point */
	void* householder_entry(void* id)
	{
		/* initialization */
		core = (uint32_t)id;

		/* run kernel */
		kernel();

		/* finished */
		while(1) {
			sched_yield();
		}
	}
#endif

/* === actual kernel =================================================== */
#define ASSERT(x) switch(0) { case 0: case x: ; }
static const int order[16] = {
	10, 11, 12, 13,  9,  8,  7, 14,  4,  5,  6, 15,  3,  2,  1,  0,
//	15, 14, 13, 12,  8,  9, 10,  6,  5,  4,  0,  1,  2,  3,  7, 11,
};

static void kernel(void)
{
	/* commlib initialization */
	comm_handle_t inL = NULL, outL = NULL, inR = NULL, outR = NULL;
	comm_init(shm.channels, core,
		commlib_heap, sizeof(commlib_heap));

	/* fetch communication handles and synchronize */
	inR  = comm_get_rhandle(order[core]);
	outR = comm_get_whandle(order[core] + 16);
	if(order[core] < NCORES-1) {
		outL = comm_get_whandle(order[core] + 1);
		inL  = comm_get_rhandle(order[core] + 17);
	}
	BARRIER;

	/* early exit for unused cores */
	if(order[core] >= NCORES)
		return;

	/* guarantee integer number of columns per core */
	const int cols = MSIZE/NCORES;
	ASSERT((cols * NCORES) == MSIZE);

	/* local data structures */
	float block[cols][MSIZE];
	float vec[MSIZE];

	/* read input block, then forward remaining blocks */
	for(int c = 0; c < cols; c++)
		comm_read(inR, &block[cols-c-1][0], 1);
	for(int b = order[core]; b < NCORES-1; b++) {
		for(int c = 0; c < cols; c++) {
			comm_read(inR,   &vec[0], 1);
			comm_write(outL, &vec[0], 1);
		}
	}

	/* handle previous weight vectors */
	for(int i = 0; i < order[core]; i++) {
		for(int c = cols-1; c >= 0; c--) {
			int k = cols * i + cols - c - 1;

			/* forward them */
			comm_read(inR, &vec[0], 1);
			if(order[core] != NCORES-1)
				comm_write(outL, &vec[0], 1);

			/* apply locally */
			for(int rc = cols-1; rc >= 0; rc--) {
				float beta = 0;
				for(int rk = k; rk < MSIZE; rk++)
					beta += vec[rk] * block[rc][rk];
				for(int rk = k; rk < MSIZE; rk++)
					block[rc][rk] -= beta * vec[rk];
			}
		}
	}

	/* handle own weight vectors */
	for(int c = cols-1; c >= 0; c--) {
		unsigned int k = cols * order[core] + cols - c - 1;

		/* calculate */
		float norm2 = 0;
		for(int i = k; i < MSIZE; i++) {
			vec[i] = block[c][i];
			norm2 += vec[i] * vec[i];
		}
		float s = sqrtf(norm2);
		vec[k] -= s;
		float denom = sqrtf(norm2 - s * block[c][k]);
		float beta = 0;
		for(int i = k; i < MSIZE; i++) {
			vec[i] /= denom;
			beta   += vec[i] * block[c][i];
		}

		/* forward */
		if(order[core] < NCORES-1)
			comm_write(outL, &vec[0], 1);

		/* apply to current column */
		for(int i = k; i < MSIZE; i++)
			block[c][i] -= beta * vec[i];

		/* apply to remaining columns in current block */
		for(int rc = c-1; rc >= 0; rc--) {
			beta = 0;
			for(int rk = k; rk < MSIZE; rk++)
				beta += vec[rk] * block[rc][rk];
			for(int rk = k; rk < MSIZE; rk++)
				block[rc][rk] -= beta * vec[rk];
		}
	}

	/* write output block, then forward remaining blocks */
	for(int c = 0; c < cols; c++)
		comm_write(outR, &block[cols-c-1], 1);
	for(int b = order[core]; b < NCORES-1; b++) {
		for(int c = 0; c < cols; c++) {
			comm_read(inL,   &vec[0], 1);
			comm_write(outR, &vec[0], 1);
		}
	}

	/* tell host program we're done */
	if(order[core] == 0) {
		shm.flag = 1;
	}
}
