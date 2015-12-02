include $(MICROPY_DIR)/py/mkenv.mk
-include mpconfigport.mk

# qstr definitions (must come before including py.mk)
QSTR_DEFS = qstrdefsport.h

# OS name, for simple autoconfig
UNAME_S := $(shell uname -s)

# include py core make definitions
include $(MICROPY_DIR)/py/py.mk

INC +=  -I.
INC +=  -I$(MICROPY_DIR)
INC += -I$(BUILD)

# compiler settings
CWARN = -Wall
CWARN += -Wpointer-arith -Wuninitialized
COPT = -O2 -DNDEBUG
CFLAGS = $(INC) $(CWARN) -ansi -std=gnu99 -DUNIX $(CFLAGS_MOD) $(COPT) $(CFLAGS_EXTRA)

LDFLAGS = $(LDFLAGS_MOD) $(LDFLAGS_ARCH) -lm $(LDFLAGS_EXTRA)
LDFLAGS += -static -lpthread

ifeq ($(MICROPY_USE_READLINE),1)
INC +=  -I$(MICROPY_DIR)/lib/mp-readline
CFLAGS_MOD += -DMICROPY_USE_READLINE=1
LIB_SRC_C_EXTRA += mp-readline/readline.c
endif
ifeq ($(MICROPY_USE_READLINE),2)
CFLAGS_MOD += -DMICROPY_USE_READLINE=2
LDFLAGS_MOD += -lreadline
# the following is needed for BSD
#LDFLAGS_MOD += -ltermcap
endif
ifeq ($(MICROPY_PY_TIME),1)
CFLAGS_MOD += -DMICROPY_PY_TIME=1
SRC_MOD += modtime.c
endif
ifeq ($(MICROPY_PY_TERMIOS),1)
CFLAGS_MOD += -DMICROPY_PY_TERMIOS=1
SRC_MOD += modtermios.c
endif
ifeq ($(MICROPY_PY_SOCKET),1)
CFLAGS_MOD += -DMICROPY_PY_SOCKET=1
SRC_MOD += modsocket.c
endif

ifeq ($(MICROPY_PY_FFI),1)
LIBFFI_CFLAGS_MOD := $(shell pkg-config --cflags libffi)
LIBFFI_LDFLAGS_MOD := $(shell pkg-config --libs libffi)
endif

ifeq ($(UNAME_S),Linux)
LIBFFI_LDFLAGS_MOD += -ldl
endif

CFLAGS_MOD += $(LIBFFI_CFLAGS_MOD) -DMICROPY_PY_FFI=1
LDFLAGS_MOD += $(LIBFFI_LDFLAGS_MOD)
SRC_MOD += modffi.c

# source files
SRC_C = \
        missing.c \
	gccollect.c \
	unix_mphal.c \
	input.c \
	file.c \
	modos.c \
	moduselect.c \
	alloc.c \
	coverage.c \
	$(SRC_MOD)

LIB_SRC_C = $(addprefix lib/,\
	$(LIB_SRC_C_EXTRA) \
	)

OBJ = $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(LIB_SRC_C:.c=.o))

include $(MICROPY_DIR)/py/mkrules.mk

.PHONY: test

# install micropython in /usr/local/bin
TARGET = micropython
# PREFIX = $(DESTDIR)/usr/local
PREFIX = ./install_dir
BINDIR = $(PREFIX)/bin

install: lib-fast
	install -D $(TARGET) $(BINDIR)/$(TARGET)

# uninstall micropython
uninstall:
	-rm $(BINDIR)/$(TARGET)

# build synthetically fast interpreter for benchmarking
lib-fast:
	$(MAKE) COPT="-O2 -DNDEBUG -fno-crossjumping" CFLAGS_EXTRA='-DMP_CONFIGFILE="<mpconfigport_fast.h>"' BUILD=build PROG=dummy lib

# Value of configure's --host= option (required for cross-compilation).
# Deduce it from CROSS_COMPILE by default, but can be overriden.
ifneq ($(CROSS_COMPILE),)
CROSS_COMPILE_HOST = --host=$(patsubst %-,%,$(CROSS_COMPILE))
else
CROSS_COMPILE_HOST =
endif
