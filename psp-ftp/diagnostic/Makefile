TARGET = PSPStreamFTP
OBJS = inert.o
PSP_FW_VERSION = 661
BUILD_PRX = 1
USE_KERNEL_LIBS = 1
# Intentionally no imports exist in this diagnostic.
NO_FIXUP_IMPORTS = 1
CFLAGS = -O2 -G0 -fno-pic -Wall -Wextra
LDFLAGS = -nostartfiles
PRX_EXPORTS = ../exports.exp
PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
