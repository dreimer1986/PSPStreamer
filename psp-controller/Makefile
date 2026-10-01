TARGET = PSPConsolizer
OBJS = main.o
PSP_FW_VERSION = 661
BUILD_PRX = 1
USE_KERNEL_LIBS = 1
CFLAGS = -O2 -G0 -Wall -Wextra
LDFLAGS = -nostartfiles
LIBS = -lpspkernel
PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
main.o: report_config.h
