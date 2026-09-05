TARGET = PSPStreamer
OBJS = main.o h264_decoder.o h264_hw.o mpeg_extra.o
PSP_FW_VERSION = 661
BUILD_PRX = 1
PSP_EBOOT_TITLE = PSP Streamer
PSP_EBOOT_ICON = assets/icon0.png
PSP_EBOOT_PIC1 = assets/pic1.png

CFLAGS = -O2 -G0 -Wall -Wextra
# The compiler specs and build.mak already provide utility and network stubs.
# Linking either a second time produces non-contiguous import tables on PSP.
## Point this at the PSP-built OpenH264 headers and libopenh264_dec_psp.a.
OPENH264_DIR ?= third_party/openh264
CXXFLAGS = -O2 -G0 -Wall -Wextra -fno-exceptions -fno-rtti -I$(OPENH264_DIR)
LIBS = -lpspjpeg -lpspaudio -lpspaudiocodec -lpsppower -lpspkubridge -lpspmpeg -lpspmpegbase -L$(OPENH264_DIR) -lopenh264_dec_psp -lstdc++ -lpthread

EXTRA_TARGETS = EBOOT.PBP
PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
