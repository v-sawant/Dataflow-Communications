/* Copyright (c) 2015 S.Raase. All rights reserved. */

/* Communication Library Header (Host & Device)
   NOTE: this file is shared across architectures! */
#ifndef _COMMLIB_H_
#define _COMMLIB_H_
#include <stddef.h>
#include <stdint.h>
#include "commlib_cfg.h"

/* ==================================================================
   = some sanity checking                                           =
   ================================================================== */
#define COMM_ALIGN(x) __attribute__((aligned(x)))
#define COMM_PACKED   __attribute__((packed))

#if !defined COMM_NUM_CHANNELS
#	error Please #define COMM_NUM_CHANNELS in commlib_cfg.h
#endif /* COMM_NUM_CHANNELS */

#if (defined COMM_EPIPHANY && !defined __epiphany__)
#	define COMM_ON_HOST
#elif (defined COMM_EPIPHANY && defined __epiphany__)
#	define COMM_ON_DEVICE
#elif (defined COMM_PTHREAD)
#	define COMM_ON_HOST
#	define COMM_ON_DEVICE
#else
#	error unsupported architecture
#endif

/* ==================================================================
   = common host & device data structures                           =
   ================================================================== */
typedef enum {
	COMM_CTYPE_INVALID = 0,		/* invalid table entry */
#ifdef COMM_CFG_CTYPE_DEFAULT
	COMM_CTYPE_DEFAULT,		/* default ring buffer */
#endif /* COMM_CFG_CTYPE_DEFAULT */
#ifdef COMM_CFG_CTYPE_HOST
	COMM_CTYPE_HOST,		/* shared memory buffer */
#endif /* COMM_CFG_CTYPE_HOST */
} comm_ctype_t;

typedef struct {
	int32_t  core;
	void*    dptr;			/* NOTE: assumes 32-bit pointers */
	void*    hptr;			/*       on both sides!          */
} COMM_ALIGN(8) comm_address_t;

/* channel description */
typedef struct {
	comm_ctype_t   type;		/* channel type */
	comm_address_t src;		/* source       */
	comm_address_t dst;		/* destination  */
	uint32_t       tsize;		/* token size   */
	uint32_t       tnum;		/* number of tokens per buffer */
} COMM_ALIGN(8) comm_channel_t;

#ifdef COMM_CFG_CTYPE_HOST
	/* HOST communication structure, host-side */
	typedef struct {
		int32_t rp;
		int32_t wp;
		uint8_t COMM_ALIGN(8) buf[];
	} COMM_ALIGN(8) COMM_PACKED comm_chost_shm_t;
#endif /* COMM_CFG_CTYPE_HOST */

/* ==================================================================
   = host-specific parts                                            =
   ================================================================== */
#ifdef COMM_ON_HOST
	/* Host API declaration */
	int  comm_host_init  (comm_channel_t[]);
	void comm_host_handle(comm_channel_t[], void*);
	void comm_host_dump  (comm_channel_t[]);

	/* Table initializer helpers */
	#ifdef COMM_CFG_CTYPE_DEFAULT
		#define DEFAULT(FROM, TO, TSIZE, TNUM) \
			{ COMM_CTYPE_DEFAULT,          \
			  { FROM, 0, 0 },              \
			  { TO,   0, 0 },              \
			  TNUM, TSIZE, }
	#endif

	#ifdef COMM_CFG_CTYPE_HOST
		#define HOST_INPUT(FILENAME, CORE, BUF, TSIZE, TNUM)         \
			{ COMM_CTYPE_HOST,                                   \
			  { (-1), (void*)offsetof(shm_t, BUF),               \
			    &((comm_ctype_host_dsc_t) { (-1), FILENAME }) }, \
			  { CORE },                                          \
			  TNUM, TSIZE, }

		#define HOST_OUTPUT(CORE, FILENAME, BUF, TSIZE, TNUM)        \
			{ COMM_CTYPE_HOST,                                   \
			  { CORE },                                          \
			  { (-1), (void*)offsetof(shm_t, BUF),               \
			    &((comm_ctype_host_dsc_t) { (-1), FILENAME }) }, \
			  TNUM, TSIZE, }

		/* descriptor type */
		typedef struct {
			int      fd;	/* descriptor of file */
			char     *file;	/* filename */
			uint64_t count;	/* tokens transmitted */
		} COMM_PACKED comm_ctype_host_dsc_t;
	#endif
#endif /* COMM_IS_HOST */

/* ==================================================================
   = device-specific parts                                          =
   ================================================================== */
#ifdef COMM_ON_DEVICE
	/* opaque communication handle */
	typedef void* comm_handle_t;

	/* Device API declaration */
	int           comm_init(volatile comm_channel_t *, int, void *, size_t);
	comm_handle_t comm_get_rhandle(int);
	comm_handle_t comm_get_whandle(int);
	int           comm_read(comm_handle_t,  void *, size_t);
	int           comm_peek(comm_handle_t,  void *, size_t);
	int           comm_write(comm_handle_t, void *, size_t);
	int           comm_level(comm_handle_t);
	int           comm_space(comm_handle_t);

	/* channel access functions */
	typedef int (*readfn_t)(comm_handle_t, void*, size_t);
	typedef int (*peekfn_t)(comm_handle_t, void*, size_t);
	typedef int (*writefn_t)(comm_handle_t, void*, size_t);
	typedef int (*levelfn_t)(comm_handle_t);
	typedef int (*spacefn_t)(comm_handle_t);

	/* local base class */
	typedef struct {
		comm_ctype_t type;
		int          tsize;
		int          tnum;
		readfn_t     readfn;
		peekfn_t     peekfn;
		writefn_t    writefn;
		levelfn_t    levelfn;
		spacefn_t    spacefn;
	} COMM_ALIGN(8) comm_data_t;

	#ifdef COMM_CFG_CTYPE_DEFAULT
		/* DEFAULT communication structures */
		typedef struct comm_cdefault_dst_s {	/* destination end */
			comm_data_t data;
			struct comm_cdefault_src_s *src;
			int rp;
			int pp;
			volatile int wp;
			char *buf;
		} COMM_ALIGN(8) comm_cdefault_dst_t;
		typedef struct comm_cdefault_src_s {	/* source end */
			comm_data_t data;
			struct comm_cdefault_dst_s *dst;
			volatile int rp;
			int wp;
			char *buf;
		} COMM_ALIGN(8) comm_cdefault_src_t;
	#endif /* COMM_CFG_CTYPE_DEFAULT */

	#ifdef COMM_CFG_CTYPE_HOST
		/* HOST communication structure, core-side */
		typedef struct {
			comm_data_t data;
			int               rp;
			int               pp;
			int               wp;
			volatile int32_t *rpp;
			volatile int32_t *wpp;
			uint8_t          *buf;
		} COMM_ALIGN(8) comm_chost_core_t;
	#endif /* COMM_CFG_CTYPE_HOST */
#endif /* COMM_IS_DEVICE */

#endif /* _COMMLIB_H_ */

