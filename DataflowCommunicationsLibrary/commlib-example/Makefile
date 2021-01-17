# Makefile

# select target: epiphany or pthread
TARGET=epiphany

# folders
HSRC	:= hsrc
HDEST	:= hobj
ESRC	:= esrc
EDEST	:= eobj
DEST	:= bin

# applications (host binary and epiphany object files) to build
HAPP	:= $(DEST)/ep_main
EOBJS	:= $(EDEST)/householder.o

# object files to build
HOBJS	:= $(HDEST)/main.o $(HDEST)/commlib-host.o $(HDEST)/epiphany-dump.o
ECOMMON	:= $(EDEST)/commlib.o

# === Toolchain Selection =================================================
ifeq ($(TARGET),epiphany)
	# host toolchain
	HCC	:= gcc
	HCFLAGS	:= -O3 -std=c99 -Wall -DCOMM_EPIPHANY \
			-I$(EPIPHANY_HOME)/tools/host/include
	HLFLAGS	:= -L$(EPIPHANY_HOME)/tools/host/lib -le-hal -le-loader
	ECHO	:= /bin/echo -e

	# target toolchain
	ECC	:= e-gcc
	EOC	:= e-objcopy
	ECFLAGS	:= -O3 -std=c99 -Wall -DCOMM_EPIPHANY \
			-g -falign-loops=8 -falign-functions=8 \
			-fsingle-precision-constant -ffast-math
	ELFLAGS	:= -T$(EPIPHANY_HOME)/bsps/current/internal.ldf -lm -le-lib
	EOFLAGS	:= -R .shared_dram

	# build ELF files
	EAPPS	:= $(EOBJS:$(EDEST)%o=$(DEST)%elf)
endif

ifeq ($(TARGET),pthread)
	# host toolchain
	HCC	:= gcc
	HCFLAGS	:= -O3 -std=gnu99 -Wall -DCOMM_PTHREAD -pthread
	HLFLAGS	:= -lm -pthread
	ECHO	:= /bin/echo -e

	# work-around for x86_64, requires multilib
	# (commlib requires 32-bit host)
	ifeq ($(shell uname -m),x86_64)
		HCC += -m32
	endif

	# target toolchain
	ECC	:= $(HCC)
	ECFLAGS	:= $(HCFLAGS)
	ELFLAGS	:= $(HLFLAGS)
	EOFLAGS	:=

	# tie host and device objects together
	# don't build device binaries
	HOBJS  += $(EOBJS) $(ECOMMON)
	EAPPS	:=
endif

ifndef HCC
	$(error Invalid target selection.)
endif

# === Rules ===============================================================
.SECONDARY:
.PHONY: help all host target folders run clean

help:
	@$(ECHO)
	@$(ECHO) "Epiphany Makefile - Help"
	@$(ECHO) "  help    show this help"
	@$(ECHO) "  host    build host application      ($(HAPP))"
	@$(ECHO) "  target  build epiphany applications ($(EAPPS))"
	@$(ECHO) "  all     build host and target"
	@$(ECHO) "  run     build all, then run host application"
	@$(ECHO) "  clean   remove applications and intermediate files"
	@$(ECHO)

all: host target

host: folders $(HAPP)

target: folders $(EAPPS)

folders: $(HDEST) $(EDEST) $(DEST)

run: host target
	@$(ECHO) "    RUN"
	@sudo LD_LIBRARY_PATH=$(LD_LIBRARY_PATH) \
	      EPIPHANY_HDF=$(EPIPHANY_HDF) \
	      $(HAPP)

clean:
	@$(ECHO) "    CLEAN"
	@rm -v -f $(HOBJS) $(ECOMMON) $(EOBJS) $(EAPPS) $(HAPP) $(EAPPS)
	@rmdir -v --ignore-fail-on-non-empty $(HDEST) $(EDEST) $(DEST)

$(HDEST):
	@$(ECHO) "    MKDIR $@"
	@mkdir -p $(HDEST)

$(EDEST):
	@$(ECHO) "    MKDIR $@"
	@mkdir -p $(EDEST)

$(DEST):
	@$(ECHO) "    MKDIR $@"
	@mkdir -p $(DEST)

# === Host Toolchain ======================================================
$(HAPP): $(HOBJS)
	@$(ECHO) "    (HOST)   LINK $@"
	@$(HCC) -o $@ $^ $(HLFLAGS)

$(HDEST)/%.o: $(HSRC)/%.c
	@$(ECHO) "    (HOST)   CC   $@"
	@$(HCC) $(HCFLAGS) -c -o $@ $<

# === Target Toolchain ====================================================
$(DEST)/%.elf: $(EDEST)/%.o $(ECOMMON)
	@$(ECHO) "    (TARGET) LINK $@"
	@$(ECC) -o $@ $^ $(ELFLAGS)
	@$(EOC) $(EOFLAGS) $@ 2>/dev/null

$(EDEST)/%.o: $(ESRC)/%.c
	@$(ECHO) "    (TARGET) CC   $@"
	@$(ECC) $(ECFLAGS) -c -o $@ $<
# =========================================================================

