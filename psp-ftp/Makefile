TARGET = PSPStreamFTP
OBJS = main.o
PSP_FW_VERSION = 661
BUILD_PRX = 1
USE_KERNEL_LIBS = 1
CFLAGS = -O2 -G0 -fno-pic -Wall -Wextra -fstack-usage
LDFLAGS = -nostartfiles
LIBS = -lpspsysmem_kernel_660 -lpspkernel
PRX_EXPORTS = exports.exp
PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
main.o: path.h size_text.h startup.h ../streammaster/protocol.h ../streammaster/tcp_server.h
