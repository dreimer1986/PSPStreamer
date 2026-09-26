TARGET = StreamerOC
OBJS = main.o
PSP_FW_VERSION = 661
BUILD_PRX = 1
USE_KERNEL_LIBS = 1
CFLAGS = -O2 -G0 -Wall -Wextra
LDFLAGS = -nostartfiles
LIBS = -lpsppower_driver -lpspge_driver -lpspsystemctrl_kernel -lgcc
PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
main.o: clock_math.h power_callback_slot.h config_parse.h report_io.h overlay_pixels.h overlay_vblank.h control_api.h ratio_transition.h clock_transition.h
main.o: overlay_hook.h
main.o: overlay_lifetime.h
main.o: sony_startup.h
main.o: clock_diagnostic.h
main.o: clock_settle.h
