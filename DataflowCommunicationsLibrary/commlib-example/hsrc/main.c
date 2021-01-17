/* Host Application */
#define _POSIX_SOURCE	/* sigaction */
#define _BSD_SOURCE	/* usleep    */
#define _GNU_SOURCE	/* pthread_tryjoin_np */

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#ifdef COMM_EPIPHANY
	#include <e-hal.h>
	#include <e-loader.h>
	extern void epiphany_dump(e_epiphany_t*, char**);

	#define COMM_HOST_HANDLE(CHANNELS) \
		do { comm_host_handle(CHANNELS, &emem); } while(0);
#endif

#ifdef COMM_PTHREAD
	#include <pthread.h>
	#include <errno.h>	/* EBUSY */

	#define COMM_HOST_HANDLE(CHANNELS) \
		do { comm_host_handle(CHANNELS, &shm); } while(0);
#endif

#include "../commlib.h"
#include "../shared.h"

/* global stuff */
shm_t shm;
#define FAIL(...)   do { fprintf(stderr, __VA_ARGS__); exit(1); } while(0);
#define PRINTF(...) do { fprintf(stderr, __VA_ARGS__);          } while(0);
volatile int sigquit_flag = 0;
void sigquit(int param) { sigquit_flag = 1; }

/* commlib channel table */
#define TOKEN_NUM  2
#define TOKEN_SIZE (MSIZE * sizeof(float))
comm_channel_t channels[COMM_NUM_CHANNELS] = {
	/* input chain */
	HOST_INPUT("input.bin", 15, input_buf,
		((HOSTBUFSIZE-128)/TOKEN_SIZE), TOKEN_SIZE),	/*  0 */
	DEFAULT(15, 14, TOKEN_NUM, TOKEN_SIZE),			/*  1 */
	DEFAULT(14, 13, TOKEN_NUM, TOKEN_SIZE),			/*  2 */
	DEFAULT(13, 12, TOKEN_NUM, TOKEN_SIZE),			/*  3 */
	DEFAULT(12,  8, TOKEN_NUM, TOKEN_SIZE),			/*  4 */
	DEFAULT( 8,  9, TOKEN_NUM, TOKEN_SIZE),			/*  5 */
	DEFAULT( 9, 10, TOKEN_NUM, TOKEN_SIZE),			/*  6 */
	DEFAULT(10,  6, TOKEN_NUM, TOKEN_SIZE),			/*  7 */
	DEFAULT( 6,  5, TOKEN_NUM, TOKEN_SIZE),			/*  8 */
	DEFAULT( 5,  4, TOKEN_NUM, TOKEN_SIZE),			/*  9 */
	DEFAULT( 4,  0, TOKEN_NUM, TOKEN_SIZE),			/* 10 */
	DEFAULT( 0,  1, TOKEN_NUM, TOKEN_SIZE),			/* 11 */
	DEFAULT( 1,  2, TOKEN_NUM, TOKEN_SIZE),			/* 12 */
	DEFAULT( 2,  3, TOKEN_NUM, TOKEN_SIZE),			/* 13 */
	DEFAULT( 3,  7, TOKEN_NUM, TOKEN_SIZE),			/* 14 */
	DEFAULT( 7, 11, TOKEN_NUM, TOKEN_SIZE),			/* 15 */

	/* output chain */
	HOST_OUTPUT(15, "output.bin", output_buf,
		((HOSTBUFSIZE-128)/TOKEN_SIZE), TOKEN_SIZE),	/* 16 */
	DEFAULT(14, 15, TOKEN_NUM, TOKEN_SIZE),			/* 17 */
	DEFAULT(13, 14, TOKEN_NUM, TOKEN_SIZE),			/* 18 */
	DEFAULT(12, 13, TOKEN_NUM, TOKEN_SIZE),			/* 19 */
	DEFAULT( 8, 12, TOKEN_NUM, TOKEN_SIZE),			/* 20 */
	DEFAULT( 9,  8, TOKEN_NUM, TOKEN_SIZE),			/* 21 */
	DEFAULT(10,  9, TOKEN_NUM, TOKEN_SIZE),			/* 22 */
	DEFAULT( 6, 10, TOKEN_NUM, TOKEN_SIZE),			/* 23 */
	DEFAULT( 5,  6, TOKEN_NUM, TOKEN_SIZE),			/* 24 */
	DEFAULT( 4,  5, TOKEN_NUM, TOKEN_SIZE),			/* 25 */
	DEFAULT( 0,  4, TOKEN_NUM, TOKEN_SIZE),			/* 26 */
	DEFAULT( 1,  0, TOKEN_NUM, TOKEN_SIZE),			/* 27 */
	DEFAULT( 2,  1, TOKEN_NUM, TOKEN_SIZE),			/* 28 */
	DEFAULT( 3,  2, TOKEN_NUM, TOKEN_SIZE),			/* 29 */
	DEFAULT( 7,  3, TOKEN_NUM, TOKEN_SIZE),			/* 30 */
	DEFAULT(11,  7, TOKEN_NUM, TOKEN_SIZE),			/* 31 */
};

/* list of kernels to load */
#ifdef COMM_EPIPHANY
	char* kernels[CORES] = {
		"bin/householder.elf", "bin/householder.elf",
		"bin/householder.elf", "bin/householder.elf",
		"bin/householder.elf", "bin/householder.elf",
		"bin/householder.elf", "bin/householder.elf",
		"bin/householder.elf", "bin/householder.elf",
		"bin/householder.elf", "bin/householder.elf",
		"bin/householder.elf", "bin/householder.elf",
		"bin/householder.elf", "bin/householder.elf",
	};
#endif
#ifdef COMM_PTHREAD
	extern void* householder_entry(void*);

	void*(*kernels[CORES])(void*) = {
		householder_entry, householder_entry,
		householder_entry, householder_entry,
		householder_entry, householder_entry,
		householder_entry, householder_entry,
		householder_entry, householder_entry,
		householder_entry, householder_entry,
		householder_entry, householder_entry,
		householder_entry, householder_entry,
	};
#endif

/* main program */
int main(int argc, char *argv[])
{
	struct sigaction sigquitaction;

	/* initialize shared memory structure */
	memset(&shm, 0, sizeof(shm_t));
	memcpy(&shm.channels, &channels, sizeof(shm.channels));
	comm_host_init(shm.channels);

#ifdef COMM_EPIPHANY
	#define SHM_OFFSET 0x01000000
	e_epiphany_t dev;
	e_mem_t      emem;
	e_set_host_verbosity(H_D0);
	e_set_loader_verbosity(L_D0);

	/* init and open epiphany workgroup, allocate shared memory */
	if(e_init(NULL) != E_OK)
		FAIL("Can't e_init()!\n");
	e_reset_system();
	if(e_open(&dev, 0, 0, 4, 4) != E_OK)
		FAIL("Can't e_open()!\n");
	if(e_alloc(&emem, SHM_OFFSET, sizeof(shm_t)) != E_OK)
		FAIL("Can't e_alloc()!\n");

	/* write shared memory structure */
	if(e_write(&emem, 0, 0, (off_t)0, &shm, sizeof(shm_t)) == E_ERR)
		FAIL("Can't e_write() full shm!\n");
#endif

	/* initially fill commlib channels */
	COMM_HOST_HANDLE(shm.channels);
	PRINTF("\r\033[0K");	/* kill output by COMM_HOST_HANDLE() */

#ifdef COMM_EPIPHANY
	/* load programs */
	for(int i = 0; i < CORES_X; i++)
		for(int j = 0; j < CORES_Y; j++)
			if(e_load(kernels[i*CORES_Y+j], &dev, i, j, E_TRUE) !=
				E_OK) FAIL("Can't load core (%i,%i)\n", i, j);
#endif

#ifdef COMM_PTHREAD
	/* start threads */
	pthread_t threads[CORES];
	for(int i = 0; i < CORES; i++)
		if(pthread_create(&threads[i], NULL, kernels[i], (void*)i))
			FAIL("Can't create thread (%i)\n", i);
#endif

	/* install signal handler */
	sigquitaction.sa_handler = sigquit;
	if(sigaction(SIGQUIT, &sigquitaction, NULL))
		FAIL("Can't install SIGQUIT handler!\n");

	/* ============================================================= */
	PRINTF("Polling shared memory. Press CTRL+\\ to dump state.\n");
	while(1) {
		/* check if SIGQUIT happened and handle it */
		if(sigquit_flag) {
			#if COMM_EPIPHANY
				e_read(&emem,0,0,(off_t)0, &shm, sizeof(shm));
				comm_host_dump(shm.channels);
				epiphany_dump(&dev, kernels);
			#else
				comm_host_dump(shm.channels);
			#endif

			sigquit_flag = 0;
			sigaction(SIGQUIT, &sigquitaction, NULL);
		}

		/* handle commlib channels */
		COMM_HOST_HANDLE(shm.channels);

		#ifdef COMM_EPIPHANY
			/* read flag from shared memory */
			if(e_read(&emem, 0, 0, (off_t)0, &shm,
				sizeof(uint32_t)) == E_ERR)
					FAIL("Can't poll!\n");
		#endif

		/* check if flag set */
		if(shm.flag != 0)
			break;

		usleep(10000);
	}

	COMM_HOST_HANDLE(shm.channels);
	PRINTF("\nProgram finished, status = %u [0x%x].\n", shm.flag, shm.flag);
	/* ============================================================= */

	/* uninstall signal handler */
	sigquitaction.sa_handler = SIG_DFL;
	if(sigaction(SIGQUIT, &sigquitaction, NULL))
		FAIL("Can't uninstall SIGQUIT handler!\n");

#ifdef COMM_EPIPHANY
	/* read full shared memory structure */
	if(e_read(&emem, 0, 0, (off_t)0, &shm, sizeof(shm_t)) == E_ERR)
		FAIL("Can't e_read() full shm!\n");

	/* free shared memory, close and finalize epiphany workgroup */
	if(e_free(&emem) != E_OK) FAIL("Can't e_free()!\n");
	if(e_close(&dev) != E_OK) FAIL("Can't e_close()!\n");
	if(e_finalize()  != E_OK) FAIL("Can't e_finalize()!\n");
#endif

	return(0);
}
