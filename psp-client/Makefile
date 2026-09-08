TARGET = PSPStreamer
OBJS = main.o h264_hw.o language.o menu_skin.o tvout_imports.o
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
LIBS = -lpspjpeg -lpspaudio -lpspaudiocodec -lpsppower -lpspkubridge -lpspmpeg mpeg_extra.o -lpspmpegbase

EXTRA_TARGETS = EBOOT.PBP
PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

# mpeg_extra must remain at its historical position between libmpeg and
# libmpegbase: moving it into OBJS makes PSP import stubs non-contiguous on
# 6.61.  The explicit prerequisite still makes a clean checkout buildable.
main.o: mpeg_extra.o timed_stream.h flv.h audio_lease.h sync_trace.h display_output.h tv_canvas.h music_ui.h music_remote.h tv_gui.h lcd_music.h language.h
language.o: language.h lang_en.h lang_de.h

# menu_skin.S uses .incbin so the receiver artwork is part of EBOOT.PBP.
menu_skin.o: assets/menu_skin.raw assets/menu_skin_tv.raw
