TARGET = PSPConsolizerUSB
OBJS = bridge.o
PSP_FW_VERSION = 661
BUILD_PRX = 1
USE_KERNEL_LIBS = 1
PRX_EXPORTS = ../../psp-client/streammaster_usb/exports.exp
CFLAGS = -O2 -G0 -Wall -Wextra -DSM_CONTROLLER_PLUGIN=1
LDFLAGS = -nostartfiles
LIBS = -lpspusb_driver -lpspusbbus_driver -lpsppower_driver -lpspdisplay_driver -lpspge_driver -lpspsystemctrl_kernel -lpspsysmem_kernel_660 -lpspkernel -lgcc
PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
bridge.o: ../../psp-client/streammaster_usb/bridge.c ../controller.h ../../streammaster/protocol.h ../../psp-overclock/power_callback_slot.h
bridge.o: ../overlay.h ../../psp-overclock/overlay_pixels.h
bridge.o: ../tvout.h ../../psp-overclock/overlay_hook.h ../../psp-overclock/control_api.h
bridge.o: ../../streammaster/pad_metadata.h
bridge.o: ../report_config.h ../../psp-client/streammaster_usb/bus_owner.h
bridge.o: ../home_button.h
bridge.o: ../../streammaster/gamepad_wire.h
bridge.o: ../../psp-overclock/title_rules.h ../../psp-overclock/title_rules_io.h
bridge.o: ../pops_rumble.h ../pops_serial.h ../pops_signature.h pops_payload.inc
pops_payload.o: ../pops_payload.c ../pops_serial.h
	psp-gcc -O2 -G0 -mno-abicalls -fno-pic -ffreestanding -fno-builtin -fno-stack-protector -fno-jump-tables -fno-tree-switch-conversion -fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -Wextra -c $< -o $@
pops_payload.bin: pops_payload.o
	psp-objcopy -O binary -j .text $< $@
pops_payload.inc: pops_payload.o pops_payload.bin ../embed_pops.py
	python3 ../embed_pops.py pops_payload.o pops_payload.bin $@
