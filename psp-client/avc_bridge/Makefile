TARGET = cooleyesBridge
OBJS = bridge.o me_imports.o
PSP_FW_VERSION = 661
BUILD_PRX = 1
PRX_EXPORTS = exports.exp

CFLAGS = -O2 -G0 -Wall -Wextra
LDFLAGS = -nostartfiles
LIBS = -lpspkernel

PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
