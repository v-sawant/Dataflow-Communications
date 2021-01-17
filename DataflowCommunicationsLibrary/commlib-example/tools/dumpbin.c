/* Copyright (c) 2015 S.Raase. All rights reserved. */

#include <stdio.h>
#include <unistd.h>

#define PRINTF(...) do { fprintf(stderr, __VA_ARGS__); } while(0);

int main(int argc, char *argv[])
{
	int sz;

	/* usage */
	if(argc != 2) {
		PRINTF("Dump binary matrix from stdin\n");
		PRINTF("Usage: %s <num>\n", argv[0]);
		PRINTF("  <num>: select matrix size, e.g. 16 for 16x16\n");
		return(1);
	}

	/* select matrix */
	sz = atoi(argv[1]);
	if(sz <= 0) {
		PRINTF("  ERROR: Invalid argument.\n");
		PRINTF("         Please select valid matrix size.\n");
		return(2);
	}

	/* check stdin */
	if(isatty(0)) {
		PRINTF("  ERROR: Won't read binary from keyboard.\n");
		PRINTF("         Redirect stdin.\n");
		return(3);
	}

	/* read matrix from stdin */
	float mat[sz][sz];
	int r, c;
	for(r = 0; r < sz; r++) {
		for(c = 0; c < sz; c++) {
			if(read(0, &mat[r][c], sizeof(float)) != sizeof(float)) {
				PRINTF("  ERROR: Partial read.\n");
				return(4);
			}
		}
	}

	/* dump matrix to stdout */
	for(r = 0; r < sz; r++) {
		printf("\t");
		for(c = 0; c < sz; c++) {
			printf("% .02f\t", mat[c][r]);
		}
		printf("\n");
	}
}
