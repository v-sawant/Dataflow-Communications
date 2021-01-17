/* Copyright (c) 2015 S.Raase. All rights reserved. */

/* Epiphany Debug State Dumper */
#ifdef COMM_EPIPHANY

#define _BSD_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <e-hal.h>

#include "../shared.h"

void epiphany_dump(e_epiphany_t *dev, char* kernels[CORES])
{
	char toolname[] = "/opt/adapteva/esdk/tools/e-gnu/bin/e-addr2line";
	char buf[512], *tmp;
	int  is_trap = 0;
	FILE *pipe;

	fflush(NULL);
	fprintf(stderr, "Dumping State:\n");
	for(int i = 0; i < 4; i++) {
		for(int j = 0; j < 4; j++) {
			uint32_t status, dstatus, config, pc;

			e_read(dev, i, j, E_REG_STATUS,
				&status, sizeof(status));
			e_read(dev, i, j, E_REG_DEBUGSTATUS,
				&dstatus, sizeof(dstatus));
			e_read(dev, i, j, E_REG_CONFIG,
				&config, sizeof(config));
			e_read(dev, i, j, E_REG_PC,
				&pc, sizeof(pc));

			/* core coordinates */
			fprintf(stderr, "[%2d,%2d] ", i, j);

			/* core state (halted, active or idle) */
			if(dstatus & 0x1) {
				/* HALTED: read & decode instruction
				   to see if it was TRAPnn */

				uint16_t instr;
				e_read(dev, i, j, (off_t)(pc-2),
					&instr, sizeof(instr));
				if((instr & 0x3FF) == 0x3E2) {
					/* check if first TRAP */
					if(is_trap == 0)
						is_trap = 1;

					/* print TRAP (1st one bold) */
					fprintf(stderr, "[%s%s%2u %s ",
						(is_trap == 1) ? "[1m" : "",
						"[37;44m TRP[22m ",
						(instr & 0xFC00) >> 10,
						"[0m");

					/* if first TRAP, continue */
					if(is_trap == 1) {
						uint32_t debugcmd = 0;
						e_write(dev, i, j,
							E_REG_DEBUGCMD,
							&debugcmd,
							sizeof(debugcmd)
						);
						is_trap = -1;
					}
				} else {
					fprintf(stderr, "[%s ",
						"[37;44m HALTED [0m");
				}
			} else {
				/* ACTIVE or IDLE */
				fprintf(stderr, "[%s ", (status  & 0x1) ?
					"[30;43m ACTIVE [0m" :
				        "[30;42m  IDLE  [0m");
			}

			fprintf(stderr, "%s] ",
				(config & 0x80000) ? "[37;44m INT [0m" :
				                     "[30;42m FPU [0m"
			);

			/* interrupt flag and exception cause */
			int excause = (status & 0x000F0000) >> 16;
			fprintf(stderr, "INT:[%s %s %d %s %s%s%s] ",
				(status & 0x2) ? "[37;41mOFF:" :
				                 "[30;42m ON:",
				(excause != 0) ? "[37;41m" :
				                 "[30;42m",
				excause,
				"[0m",
				(config & 0x2) ? "I" : "i",
				(config & 0x4) ? "O" : "o",
				(config & 0x8) ? "U" : "u"
			);

			/* get function name from program counter */
			snprintf(buf, 512, "%s -e %s -C -s -f 0x%x",
				toolname, kernels[i*4+j], pc);
			pipe = popen(buf, "r");
			if(!pipe) {
				fprintf(stderr, "Err: '%s'", buf);
				continue;
			}
			fprintf(stderr, "PC:[%8x] ", pc);
			if(fgets(buf, 512, pipe)) {	/* function name */
				tmp = strstr(buf, "\n");
				if(tmp)
					*tmp = '\0';
				fprintf(stderr, "Fn:[[1;37;44m%s[0m] ", buf);
			}
			if(fgets(buf, 512, pipe)) {	/* file:line */
				tmp = strstr(buf, "\n");
				if(tmp)
					*tmp = '\0';
				fprintf(stderr, "(%s) ", buf);
			}
			pclose(pipe);

			fprintf(stderr, "\n");
		}
	}
	fprintf(stderr, "End of Dump.\n");
}
#endif
