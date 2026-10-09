TARGET = PSPConsolizer
OBJS = main.o
PSP_FW_VERSION = 661
BUILD_PRX = 1
USE_KERNEL_LIBS = 1
CFLAGS = -O2 -G0 -fno-pic -Wall -Wextra
LDFLAGS = -nostartfiles
LIBS = -lpspsystemctrl_kernel -lpspsysmem_kernel_660 -lpspkernel
PRX_EXPORTS = exports.exp
PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
main.o: report_config.h
main.o: pops_audio_bootstrap.h pops_audio_link.h pops_audio_shared.h audio_probe_stats.h audio_mirror_signature.h ../psp-overclock/title_rules_io.h ../psp-overclock/title_rules.h
