/* Shared Header */
#ifndef _SHARED_H_
#define _SHARED_H_

#include <stdint.h>
#include "commlib.h"

/* avoid problems with eSDK headers */
#undef  ALIGN
#undef  PACKED
#undef  USED

/* define some useful properties */
#define ALIGN(X) __attribute__((aligned(X)))
#define PACKED   __attribute__((packed))
#define USED     __attribute__((used))

/* number of cores */
#define CORES_X 4
#define CORES_Y 4
#define CORES (CORES_X * CORES_Y)

/* householder parameters (number of cores, matrix size) */
#define NCORES 1
#define MSIZE  64

/* host channel size, must be large enough for buffers */
#define HOSTBUFSIZE (1024*1024)

/* shared memory definition */
typedef struct {
	uint32_t       ALIGN(8) flag;
	comm_channel_t ALIGN(8) channels[COMM_NUM_CHANNELS];
	uint8_t        ALIGN(8) input_buf[HOSTBUFSIZE];
	uint8_t        ALIGN(8) output_buf[HOSTBUFSIZE];
	uint32_t timers[CORES][10];
} ALIGN(8) shm_t;

#endif /* _SHARED_H_ */
