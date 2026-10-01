TARGET = StreamMasterUSB
OBJS = bridge.o
PSP_FW_VERSION = 661
BUILD_PRX = 1
USE_KERNEL_LIBS = 1
PRX_EXPORTS = ../../psp-client/streammaster_usb/exports.exp
CFLAGS = -O2 -G0 -Wall -Wextra -DSM_CONTROLLER_PLUGIN=1
LDFLAGS = -nostartfiles
LIBS = -lpspusb_driver -lpspusbbus_driver -lpsppower_driver -lpspdisplay_driver -lpspge_driver -lpspsystemctrl_kernel -lpspkernel -lgcc
PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
bridge.o: ../../psp-client/streammaster_usb/bridge.c ../controller.h ../../streammaster/protocol.h ../../psp-overclock/power_callback_slot.h
bridge.o: ../overlay.h ../../psp-overclock/overlay_pixels.h
bridge.o: ../tvout.h ../../psp-overclock/overlay_hook.h ../../psp-overclock/control_api.h
bridge.o: ../../streammaster/pad_metadata.h
bridge.o: ../report_config.h ../../psp-client/streammaster_usb/bus_owner.h
bridge.o: ../home_button.h
