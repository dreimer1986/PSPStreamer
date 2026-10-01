TARGET = StreamMasterUSB
OBJS = bridge.o
PSP_FW_VERSION = 661
BUILD_PRX = 1
PRX_EXPORTS = exports.exp
CFLAGS = -O2 -G0 -Wall -Wextra
LDFLAGS = -nostartfiles
LIBS = -lpspusb_driver -lpspusbbus_driver -lpspkernel
PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
bridge.o: ../../streammaster/protocol.h
bridge.o: ../../streammaster/pad_metadata.h
bridge.o: bus_owner.h
