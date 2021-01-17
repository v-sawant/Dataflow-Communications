/* Copyright (c) 2015 S.Raase. All rights reserved. */

/* Communication Library Source (Host) */
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "../commlib.h"

#define FAIL(...)   do { fprintf(stderr, __VA_ARGS__); exit(1); } while(0)
#define PRINTF(...) do { fprintf(stderr, __VA_ARGS__);          } while(0);

/* initialize commlib-host */
int comm_host_init(comm_channel_t channels[COMM_NUM_CHANNELS])
{
	for(size_t i = 0; i < COMM_NUM_CHANNELS; i++) {
#ifdef COMM_CFG_CTYPE_HOST
		if(channels[i].type == COMM_CTYPE_HOST) {
			int source;	/* direction, 1 if host -> device */
			comm_ctype_host_dsc_t *desc;

			/* grab descriptor for channel */
			if(channels[i].src.core == -1) {
				source = 1;
				desc   = (comm_ctype_host_dsc_t*)channels[i].src.hptr;
			} else if(channels[i].dst.core == -1) {
				source = 0;
				desc   = (comm_ctype_host_dsc_t*)channels[i].dst.hptr;
			} else {
				FAIL("ERROR: host channel %2d invalid\n", i);
			}

			/* open file if necessary */
			if(desc->fd == -1) {
				if(source) {
					/* source file */
					desc->fd = open(desc->file, O_RDONLY);
				} else {
					/* handle magic name "stdout" */
					if(!strcmp(desc->file, "stdout")) {
						desc->fd = 1;
					} else {
						desc->fd = open(desc->file,
							O_WRONLY | O_CREAT |
							O_TRUNC, 0666);
					}
				}

				/* fail on error */
				if(desc->fd == -1) {
					FAIL("ERROR: can't open '%s': %s\n",
						desc->file, strerror(errno));
				}
			}

			PRINTF("Host channel %2zu: fd %2i, %s file '%s'.\n",
				i, desc->fd,
				source ? " input" : "output", desc->file);

		}
#endif /* COMM_CFG_CTYPE_HOST */
	}

	return(0);
}

#ifdef COMM_CFG_CTYPE_HOST
	#ifdef COMM_EPIPHANY
		#include <e-hal.h>

		#define SHM_READ(DEST, OFF, SIZE, MSG) do {             \
			if(e_read(emem, 0, 0, OFF, DEST, SIZE) != SIZE) \
				FAIL(MSG);                              \
			} while(0);

		#define SHM_WRITE(SRC, OFF, SIZE, MSG) do {             \
			if(e_write(emem, 0, 0, OFF, SRC, SIZE) != SIZE) \
				FAIL(MSG);                              \
			} while(0);
	#endif

	#ifdef COMM_PTHREAD
		#define SHM_READ(DEST, OFF, SIZE, MSG) do {      \
				memcpy(DEST, shmbase+OFF, SIZE); \
			} while(0);

		#define SHM_WRITE(SRC, OFF, SIZE, MSG) do {      \
				memcpy(shmbase+OFF, SRC, SIZE);  \
			} while(0);
	#endif

	/* read from channel into file */
	static int do_read(comm_channel_t *ch, void* param)
	{
		off_t shmoff = (off_t)ch->dst.dptr;
		comm_ctype_host_dsc_t *desc =
			(comm_ctype_host_dsc_t*)ch->dst.hptr;
		comm_chost_shm_t      meta;	/* metadata: rp, wp */

		#ifdef COMM_EPIPHANY
			e_mem_t *emem = param;
		#endif

		#ifdef COMM_PTHREAD
			void* shmbase = param;
		#endif

		if(ch->dst.core != -1) FAIL("rd: invalid channel\n");
		if(desc->fd == -1)     FAIL("rd: invalid file\n");

		int tnum = ch->tnum + 1;

		/* read metadata, calculate number of tokens to read */
		SHM_READ(&meta, shmoff, sizeof(meta),
			"rd: shm-read meta\n");
		int level = (tnum + meta.wp - meta.rp) % tnum;

		/* read tokens from shm and write them to file */
		uint8_t *token = malloc(ch->tsize);
		for(int i = 0; i < level; i++) {
			off_t offset = shmoff +
				sizeof(comm_chost_shm_t) +
				((meta.rp + i) % tnum) * ch->tsize;

			SHM_READ(token, offset, ch->tsize,
				"rd: shm-read token\n");

			if(write(desc->fd, token,
				ch->tsize) != ch->tsize)
					FAIL("rd: file-write token\n");

			desc->count++;
		}
		free(token);

		/* update metadata (rp field only) */
		int32_t newrp = (meta.rp + level) % tnum;
		off_t offset = shmoff + offsetof(comm_chost_shm_t, rp);
		SHM_WRITE(&newrp, offset, sizeof(newrp),
			"rd: shm-write meta\n");
		PRINTF("rd: (%2d/%2d | %lld) ", newrp, meta.wp, desc->count);

		/* return number of tokens read for this channel */
		return(desc->count);
	}

	/* write to a channel */
	static int do_write(comm_channel_t *ch, void* param)
	{
		off_t shmoff = (off_t)ch->src.dptr;
		comm_ctype_host_dsc_t *desc =
			(comm_ctype_host_dsc_t*)ch->src.hptr;
		comm_chost_shm_t      meta;	/* metadata: rp, wp */

		#ifdef COMM_EPIPHANY
			e_mem_t *emem = param;
		#endif

		#ifdef COMM_PTHREAD
			void* shmbase = param;
		#endif

		if(ch->src.core != -1) FAIL("wr: invalid channel\n");
		if(desc->fd == -1)     FAIL("wr: invalid file\n");

		int tnum = ch->tnum + 1;

		/* read metadata, calculate number of tokens to write */
		SHM_READ(&meta, shmoff, sizeof(meta),
			"wr: shm-read meta\n");
		int space = (tnum - 1 + meta.rp - meta.wp) % tnum;

		/* read tokens from file and write them to shm */
		uint8_t *token = malloc(ch->tsize);
		for(int i = 0; i < space; i++) {
			ssize_t ret;

			ret = read(desc->fd, token, ch->tsize);
			if(ret == 0) {
				break;	/* eof */
			} else if(ret != ch->tsize) {
				FAIL("wr: file-read token: %s (%i)\n",
					strerror(errno), ret);
			}

			off_t offset = shmoff +
				sizeof(comm_chost_shm_t) +
				((meta.wp + i) % tnum) * ch->tsize;
			SHM_WRITE(token, offset, ch->tsize,
				"wr: shm-write token\n");

			desc->count++;
		}
		free(token);

		/* update metadata (wp field only) */
		int32_t newwp = (meta.wp + space) % tnum;
		off_t offset = shmoff + offsetof(comm_chost_shm_t, wp);
		SHM_WRITE(&newwp, offset, sizeof(newwp),
			"wr: shm-write meta\n");
		PRINTF("wr: (%2d/%2d | %lld) ", meta.rp, newwp, desc->count);

		/* return number of tokens written for this channel */
		return(desc->count);
	}

	/* handle all channels */
	void comm_host_handle(comm_channel_t channels[COMM_NUM_CHANNELS],
		void* param)
	{
		PRINTF("commlib-host: ");
		for(size_t i = 0; i < COMM_NUM_CHANNELS; i++) {
			if(channels[i].type != COMM_CTYPE_HOST)
				continue;

			if(!param)
				FAIL("comm_host_handle: missing param!\n");

			/* do_read, do_write will print status */
			if(channels[i].dst.core == -1) {
				do_read(&channels[i], param);
			} else if(channels[i].src.core == -1) {
				do_write(&channels[i], param);
			}
		}
		PRINTF("\r"); fflush(NULL);
	}
#endif /* COMM_CFG_CTYPE_HOST */

/* dump channel structure */
void comm_host_dump(comm_channel_t channels[COMM_NUM_CHANNELS])
{
	PRINTF("Channel configuration:\n");
	for(size_t i = 0; i < COMM_NUM_CHANNELS; i++) {
		switch(channels[i].type) {
#ifdef COMM_CFG_CTYPE_DEFAULT
		case COMM_CTYPE_DEFAULT:
			PRINTF("DEFAULT [%2zu]: %5d * %2d bytes  |  "
				"[0x%8x] [0x%8x]  |  %2d -> %2d\n",
				i,
				channels[i].tnum, channels[i].tsize,
				(uint32_t)channels[i].src.dptr,
				(uint32_t)channels[i].dst.dptr,
				channels[i].src.core, channels[i].dst.core);
			break;
#endif /* COMM_CFG_CTYPE_DEFAULT */
#ifdef COMM_CFG_CTYPE_HOST
		case COMM_CTYPE_HOST:
			if(channels[i].src.core == -1) {
				PRINTF("HOST    [%2zu]: %5d * %2d bytes  |  "
					"[0x%8x]  |  '%s' (fd %d @ %d) -> %2d\n",
					i,
					channels[i].tnum, channels[i].tsize,
					(uint32_t)channels[i].dst.dptr,
					((comm_ctype_host_dsc_t*)channels[i].src.hptr)->file,
					((comm_ctype_host_dsc_t*)channels[i].src.hptr)->fd,
					(uint32_t)channels[i].src.dptr,
					channels[i].dst.core);
			} else if(channels[i].dst.core == -1) {
				PRINTF("HOST    [%2zu]: %5d * %2d bytes  |  "
					"[0x%8x]  |  %2d -> '%s' (fd %d @ %d)\n",
					i,
					channels[i].tnum, channels[i].tsize,
					(uint32_t)channels[i].src.dptr,
					channels[i].src.core,
					((comm_ctype_host_dsc_t*)channels[i].dst.hptr)->file,
					((comm_ctype_host_dsc_t*)channels[i].dst.hptr)->fd,
					(uint32_t)channels[i].dst.dptr);
			} else {
				PRINTF("HOST    [%2zu]: invalid configuration", i);
			}
			break;
#endif /* COMM_CFG_CTYPE_HOST */
		default:
			PRINTF("UNKNOWN [%2zu]: invalid channel type %d\n",
				i, channels[i].type);
			break;
		}
	}
}

