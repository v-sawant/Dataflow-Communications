/* Copyright (c) 2015 S.Raase. All rights reserved. */

/* Communication Library Source (Device) */
#include <stdlib.h>	/* NULL */
#include <string.h>	/* memcpy() */
#include "../commlib.h"

/* changelog:
   v1: initial epiphany implementation
   v2: idle support; avoid modulo operations
   v3: host channel support; pthreads support */
#define VERSION "v3"

#define TRAP_OOM     50		/* out of memory */
#define TRAP_TABLE   51		/* invalid table entry or index */
#define TRAP_INVALID 52		/* invalid access function */

/* =====================================================================
   = API declaration                                                   =
   ===================================================================== */
int comm_init(volatile comm_channel_t *ch, int id, void *hbase, size_t hsize);
comm_handle_t comm_get_rhandle(int index);
comm_handle_t comm_get_whandle(int index);
int comm_read(comm_handle_t handle, void *buf, size_t count);
int comm_peek(comm_handle_t handle, void *buf, size_t count);
int comm_write(comm_handle_t handle, void *buf, size_t count);
int comm_level(comm_handle_t handle);
int comm_space(comm_handle_t handle);

/* =====================================================================
   = Hardware Abstraction: TRAP(num), GADDR(addr), CORELOCAL           =
   ===================================================================== */
#if defined COMM_EPIPHANY
	#include <e-lib.h>

	#define DO_TRAP(num) do { __asm__ volatile ("TRAP "#num); } while(1);
	#define TRAP(num) DO_TRAP(num)

	static inline void* GADDR(void* addr) {
		unsigned row, col;
		e_coords_from_coreid(e_get_coreid(), &row, &col);
		return(e_get_global_address(row, col, addr));
	}

	#define CORELOCAL

#elif defined COMM_PTHREAD
	#include <stdio.h>
	#include <pthread.h>

	#define TRAP(num) do { \
		fprintf(stderr, "\nTRAP %i on core %i (%s:%i). Exit.\n", \
			num, core, __FILE__, __LINE__); \
		exit(1); \
	} while(1);

	#define GADDR(addr) addr

	#define CORELOCAL __thread

#else
	#error unsupported architecture (define COMM_EPIPHANY or COMM_PTHREAD)

#endif

/* =====================================================================
   = COMM_CFG_USE_IDLE: IDLEINIT(), IDLE() and WAKEUP(addr)            =
   ===================================================================== */
#ifdef COMM_CFG_USE_IDLE
	#ifdef COMM_EPIPHANY
		/* Epiphany IDLE support */
		#define	IDLEINIT() /* register SWI */ \
			do { \
				e_irq_attach(E_USER_INT, SWI_Handler); \
				e_irq_mask(E_USER_INT, E_FALSE); \
			} while(0);

		#define IDLE() /* set core to idle */ \
			do { __asm__ volatile("IDLE"); } while(0);

		#define WAKEUP(addr) /* trigger SWI remotely */ \
			do { \
				uint32_t *ILATST = (uint32_t*)(((uint32_t)addr & 0xFFF00000) | 0xF042C); \
				*ILATST = 0x200; \
			} while(0);

		/* define empty interrupt handler */
		static void __attribute__((interrupt("swi"))) SWI_Handler()
		{
		}

	#elif defined COMM_PTHREAD
		/* pthreads IDLE support */
		#warning pthreads idle support experimental
		#define IDLEINIT()
		#define IDLE() sched_yield()
		#define WAKEUP(addr)

	#else
		#error idle support unavailable

	#endif

#else
	/* IDLE disabled */
	#define IDLEINIT()
	#define IDLE()
	#define WAKEUP(addr)

#endif /* COMM_CFG_USE_IDLE */

/* =====================================================================
   = COMM_CFG_USE_MALLOC: comm_malloc(size)                            =
   ===================================================================== */
#ifdef COMM_CFG_USE_MALLOC
	/* use system malloc() */
	#define comm_malloc(size) malloc(size)

#else
	/* global variables */
	static CORELOCAL void*  heap_base = NULL;
	static CORELOCAL size_t heap_size = 0;

	/* simple memory allocator, no free support;
	   requires userspace heap */
	static void *comm_malloc(size_t size)
	{
		static CORELOCAL int offset = 0;

		if(size <= 0 || offset + size > heap_size)
			return(NULL);

		/* align to 8 bytes */
		size_t tmp = size + 7;
		while(tmp >= 8)
			tmp -= 8;
		size   += 7 - tmp;
		offset += size;

		return(heap_base + offset - size);
	}
#endif

/* globals */
static CORELOCAL volatile comm_channel_t *channels;
static CORELOCAL unsigned core;

#ifdef COMM_CFG_CTYPE_DEFAULT
/* =====================================================================
   = DEFAULT channel type helper functions                             =
   ===================================================================== */
static int cdefault_read(comm_handle_t handle, void *buf, size_t count)
{
	comm_cdefault_dst_t *port = handle;

	for(size_t i = 0; i < count; i++) {
		/* block until token ready */
		while(port->rp == port->wp)
			IDLE();

		/* read one token */
		memcpy(buf, &port->buf[port->rp * port->data.tsize],
			port->data.tsize);
		buf = (char*)buf + port->data.tsize;

		/* calculate new read pointer */
		int tmp = (port->rp + 1);
		while(tmp >= port->data.tnum)
			tmp -= port->data.tnum;

		/* update read pointer and shadow */
		port->rp      = tmp;
		port->src->rp = port->rp;

		/* wake up remote */
		WAKEUP(port->src);
	}

	return(count);
}

static int cdefault_peek(comm_handle_t handle, void *buf, size_t count)
{
	comm_cdefault_dst_t *port = handle;

	for(size_t i = 0; i < count; i++) {
		/* return if no more tokens */
		if(port->pp == port->wp)
			return(i);

		/* read one token */
		memcpy(buf, &port->buf[port->pp * port->data.tsize],
			port->data.tsize);
		buf = (char*)buf + port->data.tsize;

		/* update peek pointer */
		int tmp = port->pp + 1;
		while(tmp >= port->data.tnum)
			tmp -= port->data.tnum;
		port->pp = tmp;
	}

	/* reset peek pointer */
	port->pp = port->rp;

	return(count);
}

static int cdefault_write(comm_handle_t handle, void *buf, size_t count)
{
	comm_cdefault_src_t *port = handle;

	for(size_t i = 0; i < count; i++) {
		int tmp = port->wp + 1;
		while(tmp >= port->data.tnum)
			tmp -= port->data.tnum;

		/* block until token ready */
		while(port->rp == tmp)
			IDLE();

		/* write one token */
		memcpy(&port->buf[port->wp * port->data.tsize], buf,
			port->data.tsize);
		buf = (char*)buf + port->data.tsize;

		/* update write pointer and shadow */
		port->wp      = tmp;
		port->dst->wp = port->wp;

		/* wake up remote */
		WAKEUP(port->dst);
	}

	return(count);
}

static int cdefault_level(comm_handle_t handle)
{
	comm_cdefault_dst_t *port = handle;

	/* calculate level */
	int tmp = port->data.tnum + port->wp - port->rp;
	while(tmp >= port->data.tnum)
		tmp -= port->data.tnum;

	return(tmp);
}

static int cdefault_space(comm_handle_t handle)
{
	comm_cdefault_src_t *port = handle;

	/* calculate space */
	int tmp = port->data.tnum - 1 + port->rp - port->wp;
	while(tmp >= port->data.tnum)
		tmp -= port->data.tnum;

	return(tmp);
}

static void cdefault_create_src(volatile comm_channel_t *channel)
{
	/* allocate source port */
	comm_cdefault_src_t *port = comm_malloc(sizeof(comm_cdefault_src_t));
	if(!port) {		/* OOM */
		TRAP(TRAP_OOM);
	}

	/* initialize it */
	port->data.type    = COMM_CTYPE_DEFAULT;
	port->data.tsize   = channel->tsize;
	port->data.tnum    = channel->tnum + 1;
	port->data.readfn  = NULL;
	port->data.peekfn  = NULL;
	port->data.writefn = cdefault_write;
	port->data.levelfn = NULL;
	port->data.spacefn = cdefault_space;
	port->rp = 0;
	port->wp = 0;

	/* mark as ready and wait until it propagated */
	channel->src.dptr = port;
	while(channel->src.dptr != port);

	return;
}

static void cdefault_create_dst(volatile comm_channel_t *channel)
{
	/* allocate destination port */
	comm_cdefault_dst_t *port = comm_malloc(sizeof(comm_cdefault_dst_t));
	if(!port) {		/* OOM */
		TRAP(TRAP_OOM);
	}

	/* initialize it */
	port->data.type    = COMM_CTYPE_DEFAULT;
	port->data.tsize   = channel->tsize;
	port->data.tnum    = channel->tnum + 1;
	port->data.readfn  = cdefault_read;
	port->data.peekfn  = cdefault_peek;
	port->data.writefn = NULL;
	port->data.levelfn = cdefault_level;
	port->data.spacefn = NULL;
	port->rp  = 0;
	port->pp  = 0;
	port->wp  = 0;
	port->buf = comm_malloc(port->data.tsize * port->data.tnum);
	if(!port->buf) {	/* OOM */
		TRAP(TRAP_OOM);
	}

	/* mark as ready and wait until it propagated */
	channel->dst.dptr = port;
	while(channel->dst.dptr != port);

	return;
}

static void cdefault_connect_src(volatile comm_channel_t *channel)
{
	comm_cdefault_src_t *port = channel->src.dptr;

	/* wait for destination port */
	while(!channel->dst.dptr);

	/* grab remote address */
	port->dst = channel->dst.dptr;

	/* cache buffer address */
	port->buf = port->dst->buf;

	return;
}

static void cdefault_connect_dst(volatile comm_channel_t *channel)
{
	comm_cdefault_dst_t *port = channel->dst.dptr;

	/* wait for source port */
	while(!channel->src.dptr);

	/* grab remote address */
	port->src = channel->src.dptr;

	return;
}
#endif /* COMM_CFG_CTYPE_DEFAULT */

#ifdef COMM_CFG_CTYPE_HOST
/* =====================================================================
   = HOST channel type helper functions                                =
   ===================================================================== */
	#ifdef COMM_EPIPHANY
		#define SHM_BASE 0x8f000000
	#elif defined COMM_PTHREAD
		/* dependent on host implementation */
		#include "../shared.h"
		extern shm_t shm;
		const uint32_t SHM_BASE = (uint32_t)&shm;
	#endif /* __epiphany__ */

static int chost_read(comm_handle_t handle, void *buf, size_t count)
{
	comm_chost_core_t *port = handle;

	for(size_t i = 0; i < count; i++) {
		/* block until token ready */
		while(port->rp == *port->wpp)
			;	/* do not idle! */

		/* read one token */
		memcpy(buf, &port->buf[port->rp * port->data.tsize],
			port->data.tsize);
		buf = (char*)buf + port->data.tsize;

		/* calculate new read pointer */
		int tmp = port->rp + 1;
		while(tmp >= port->data.tnum)
			tmp -= port->data.tnum;

		/* update read pointer */
		 port->rp  = tmp;
		*port->rpp = tmp;
	}

	return(count);
}

static int chost_peek(comm_handle_t handle, void *buf, size_t count)
{
	comm_chost_core_t *port = handle;

	for(size_t i = 0; i < count; i++) {
		/* return if no more tokens */
		if(port->pp == *port->wpp)
			return(i);

		/* read one token */
		memcpy(buf, &port->buf[port->pp * port->data.tsize],
			port->data.tsize);
		buf = (char*)buf + port->data.tsize;

		/* update peek pointer */
		int tmp = port->pp + 1;
		while(tmp >= port->data.tnum)
			tmp -= port->data.tnum;
		port->pp = tmp;
	}

	/* reset peek pointer */
	port->pp = *port->rpp;

	return(count);
}

static int chost_write(comm_handle_t handle, void *buf, size_t count)
{
	comm_chost_core_t *port = handle;

	for(size_t i = 0; i < count; i++) {
		/* block until token ready */
		int tmp = port->wp + 1;
		while(tmp >= port->data.tnum)
			tmp -= port->data.tnum;

		while(*port->rpp == tmp)
			;	/* do not idle! */

		/* write one token */
		memcpy(&port->buf[port->wp * port->data.tsize], buf,
			port->data.tsize);
		buf = (char*)buf + port->data.tsize;

		/* update write pointer */
		 port->wp  = tmp;
		*port->wpp = tmp;
	}

	return(count);
}

static int chost_level(comm_handle_t handle)
{
	comm_chost_core_t *port = handle;

	/* calculate level */
	int tmp = port->data.tnum + *port->wpp - port->rp;
	while(tmp >= port->data.tnum)
		tmp -= port->data.tnum;

	return(tmp);
}

static int chost_space(comm_handle_t handle)
{
	comm_chost_core_t *port = handle;

	/* calculate space */
	int tmp = port->data.tnum - 1 + *port->rpp - port->wp;
	while(tmp >= port->data.tnum)
		tmp -= port->data.tnum;

	return(tmp);
}

static void chost_create(volatile comm_channel_t *channel, int dir)
{
	/* allocate core data structure */
	comm_chost_core_t *port = comm_malloc(sizeof(comm_chost_core_t));
	if(!port) {		/* OOM */
		TRAP(TRAP_OOM);
	}

	/* initialize it */
	comm_chost_shm_t *shm;
	port->data.type  = COMM_CTYPE_HOST;
	port->data.tsize = channel->tsize;
	port->data.tnum  = channel->tnum + 1;
	if(dir) {
		/* trap if destination is not host */
		if(channel->dst.core != -1)
			TRAP(TRAP_TABLE);

		/* output port methods */
		port->data.readfn  = NULL;
		port->data.peekfn  = NULL;
		port->data.writefn = chost_write;
		port->data.levelfn = NULL;
		port->data.spacefn = chost_space;

		/* pointer to shm structure */
		shm = (void*)(SHM_BASE + (size_t)channel->dst.dptr);
	} else {
		/* trap if source is not host */
		if(channel->src.core != -1)
			TRAP(TRAP_TABLE);

		/* input port methods */
		port->data.readfn  = chost_read;
		port->data.peekfn  = chost_peek;
		port->data.writefn = NULL;
		port->data.levelfn = chost_level;
		port->data.spacefn = NULL;

		/* pointer to shm structure */
		shm = (void*)(SHM_BASE + (size_t)channel->src.dptr);
	}

	/* trap on invalid pointer in table */
	if(!shm)
		TRAP(TRAP_TABLE);

	port->rp  = 0;
	port->pp  = 0;
	port->wp  = 0;
	port->rpp = &shm->rp;
	port->wpp = &shm->wp;
	port->buf = shm->buf;

	/* mark as ready and wait until it propagated */
	if(dir) {
		/* source end */
		channel->src.dptr = port;
		while(channel->src.dptr != port);
	} else {
		/* destination end */
		channel->dst.dptr = port;
		while(channel->dst.dptr != port);
	}

	return;
}

static void chost_create_src(volatile comm_channel_t *channel)
{
	chost_create(channel, 1);
}

static void chost_create_dst(volatile comm_channel_t *channel)
{
	chost_create(channel, 0);
}

static void chost_connect_src(volatile comm_channel_t *channel)
{
	return;		/* nothing to do */
}

static void chost_connect_dst(volatile comm_channel_t *channel)
{
	return;		/* nothing to do */
}
#endif /* COMM_CFG_CTYPE_HOST */

/* =====================================================================
   = API implementation                                                =
   ===================================================================== */
/* initializes communication structures,
   blocks until remotes ready, TRAPs on error */
int comm_init(volatile comm_channel_t *ch, int id, void *hbase, size_t hsize)
{
	/* store library information globally */
	channels  = ch;
	core      = id;
#ifndef COMM_CFG_USE_MALLOC
	heap_base = GADDR(hbase);
	heap_size = hsize;
#endif

	/* initialize IDLE framework */
	IDLEINIT();

	/* create local data structures and buffers */
	for(size_t i = 0; i < COMM_NUM_CHANNELS; i++) {
		/* channel sources */
		if(channels[i].src.core == core) {
			switch(channels[i].type) {
			case COMM_CTYPE_INVALID:
				break;
#ifdef COMM_CFG_CTYPE_DEFAULT
			case COMM_CTYPE_DEFAULT:
				cdefault_create_src(&channels[i]);
				break;
#endif /* COMM_CFG_CTYPE_DEFAULT */
#ifdef COMM_CFG_CTYPE_HOST
			case COMM_CTYPE_HOST:
				chost_create_src(&channels[i]);
				break;
#endif /* COMM_CFG_CTYPE_HOST */
			default:
				TRAP(TRAP_TABLE);
			}
		}

		/* channel destinations */
		if(channels[i].dst.core == core) {
			switch(channels[i].type) {
			case COMM_CTYPE_INVALID:
				break;
#ifdef COMM_CFG_CTYPE_DEFAULT
			case COMM_CTYPE_DEFAULT:
				cdefault_create_dst(&channels[i]);
				break;
#endif /* COMM_CFG_CTYPE_DEFAULT */
#ifdef COMM_CFG_CTYPE_HOST
			case COMM_CTYPE_HOST:
				chost_create_dst(&channels[i]);
				break;
#endif /* COMM_CFG_CTYPE_HOST */
			default:
				TRAP(TRAP_TABLE);
			}
		}
	}

	/* connect local and remote structures */
	for(size_t i = 0; i < COMM_NUM_CHANNELS; i++) {
		/* channel sources */
		if(channels[i].src.core == core) {
			switch(channels[i].type) {
			case COMM_CTYPE_INVALID:
				break;
#ifdef COMM_CFG_CTYPE_DEFAULT
			case COMM_CTYPE_DEFAULT:
				cdefault_connect_src(&channels[i]);
				break;
#endif /* COMM_CFG_CTYPE_DEFAULT */
#ifdef COMM_CFG_CTYPE_HOST
			case COMM_CTYPE_HOST:
				chost_connect_src(&channels[i]);
				break;
#endif /* COMM_CFG_CTYPE_HOST */
			default:
				TRAP(TRAP_TABLE);
			}
		}

		/* channel destinations */
		if(channels[i].dst.core == core) {
			switch(channels[i].type) {
			case COMM_CTYPE_INVALID:
				break;
#ifdef COMM_CFG_CTYPE_DEFAULT
			case COMM_CTYPE_DEFAULT:
				cdefault_connect_dst(&channels[i]);
				break;
#endif /* COMM_CFG_CTYPE_DEFAULT */
#ifdef COMM_CFG_CTYPE_HOST
			case COMM_CTYPE_HOST:
				chost_connect_dst(&channels[i]);
				break;
#endif /* COMM_CFG_CTYPE_HOST */
			default:
				TRAP(TRAP_TABLE);
			}
		}
	}

	return(1);
}

/* return read handle from global table index */
comm_handle_t comm_get_rhandle(int index)
{
	if(index < 0 || index >= COMM_NUM_CHANNELS)
		TRAP(TRAP_TABLE);

	if(channels[index].dst.core != core)
		TRAP(TRAP_TABLE);

	if(!channels[index].dst.dptr)
		TRAP(TRAP_TABLE);

	return(channels[index].dst.dptr);
}

/* return write handle from global table index */
comm_handle_t comm_get_whandle(int index)
{
	if(index < 0 || index >= COMM_NUM_CHANNELS)
		TRAP(TRAP_TABLE);

	if(channels[index].src.core != core)
		TRAP(TRAP_TABLE);

	if(!channels[index].src.dptr)
		TRAP(TRAP_TABLE);

	return(channels[index].src.dptr);
}

/* reads 'count' tokens into 'buf', may block */
int comm_read(comm_handle_t handle, void *buf, size_t count)
{
	comm_data_t *data = handle;
	if(!data || !data->readfn)
		TRAP(TRAP_INVALID);

	return(data->readfn(handle, buf, count));
}

/* copy up to 'count' tokens into 'buf' */
int comm_peek(comm_handle_t handle, void *buf, size_t count)
{
	comm_data_t *data = handle;
	if(!data || !data->peekfn)
		TRAP(TRAP_INVALID);

	return(data->peekfn(handle, buf, count));
}

/* writes 'count' tokens from 'buf', may block */
int comm_write(comm_handle_t handle, void *buf, size_t count)
{
	comm_data_t *data = handle;
	if(!data || !data->writefn)
		TRAP(TRAP_INVALID);

	return(data->writefn(handle, buf, count));
}

/* returns number of tokens readable without blocking */
int comm_level(comm_handle_t handle)
{
	comm_data_t *data = handle;
	if(!data || !data->levelfn)
		TRAP(TRAP_INVALID);

	return(data->levelfn(handle));
}

/* returns number of tokens writeable without blocking */
int comm_space(comm_handle_t handle)
{
	comm_data_t *data = handle;
	if(!data || !data->spacefn)
		TRAP(TRAP_INVALID);

	return(data->spacefn(handle));
}

