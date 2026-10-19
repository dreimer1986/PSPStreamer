TARGET = PSPStreamFTP
OBJS = loader.o
PSP_FW_VERSION = 661
BUILD_PRX = 1
USE_KERNEL_LIBS = 1
CFLAGS = -O2 -G0 -fno-pic -Wall -Wextra -fstack-usage
LDFLAGS = -nostartfiles
LIBS = -lpspsysmem_kernel_660 -lpspkernel
PRX_EXPORTS = exports.exp
PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
loader.o: loader.c startup.h
.PHONY: service
all: service
service:
	$(MAKE) -C service
