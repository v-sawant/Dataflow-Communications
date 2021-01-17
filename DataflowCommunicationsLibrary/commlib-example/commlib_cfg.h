/* Communication Library Header (Host & Device)
   NOTE: this file is shared across architectures! */
#ifndef _COMMLIB_CFG_H_
#define _COMMLIB_CFG_H_

/* number of communication channels */
#define COMM_NUM_CHANNELS 32

/* channel types to support */
#define COMM_CFG_CTYPE_DEFAULT
#define COMM_CFG_CTYPE_HOST

/* other configuration options */
#undef  COMM_CFG_USE_IDLE
#undef  COMM_CFG_USE_MALLOC

#endif /* _COMMLIB_CFG_H_ */

