TARGET = PSPStreamer
OBJS = main.o h264_hw.o language.o menu_skin.o tvout_imports.o milkdrop_warp.o milkdrop_gu.o milkdrop_preset.o preset_math.o milkdrop_signal.o milkdrop_wave.o milkdrop_wave_extra.o milkdrop_decor.o tls_transport.o milkdrop_texture.o
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
LIBS = -lpspjpeg -lpspaudio -lpspaudiocodec -lpsppower -lpspkubridge -lpspmpeg mpeg_extra.o -lpspmpegbase -lpspgu -lm -lmbedtls -lmbedx509 -lmbedcrypto -lpspsystemctrl_user -lpng -ljpeg -lz

EXTRA_TARGETS = EBOOT.PBP
PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

# mpeg_extra must remain at its historical position between libmpeg and
# libmpegbase: moving it into OBJS makes PSP import stubs non-contiguous on
# 6.61.  The explicit prerequisite still makes a clean checkout buildable.
main.o: mpeg_extra.o timed_stream.h flv.h audio_lease.h sync_trace.h display_output.h tv_canvas.h music_ui.h music_remote.h tv_gui.h lcd_music.h language.h
language.o: language.h lang_en.h lang_de.h
main.o milkdrop_warp.o milkdrop_gu.o: milkdrop_warp.h
main.o milkdrop_gu.o milkdrop_preset.o: milkdrop_preset.h milkdrop_warp.h preset_math.h milkdrop_signal.h
milkdrop_signal.o: milkdrop_signal.h
preset_math.o: preset_math.h preset_expression.h
main.o: music_preset_ui.h remote_http.h browser_remote.h remote_state.h video_watchdog.h stream_diagnostic.h spectrum_fullscreen.h
main.o: server_auth.h video_controls.h
main.o: preset_catalog.h preset_browser.h
main.o: preset_sequence.h
main.o tls_transport.o: tls_transport.h
main.o: server_connection.h app_settings.h
main.o: offline_source.h offline_ui.h offline_filename.h
main.o: offline_preroll.h mp3_preroll.h
main.o: wifi_connection.h
main.o: music_caption.h
main.o milkdrop_gu.o milkdrop_wave.o: milkdrop_wave.h milkdrop_warp.h
milkdrop_wave_extra.o: milkdrop_wave.h milkdrop_decor.h milkdrop_warp.h
main.o milkdrop_gu.o milkdrop_wave.o milkdrop_preset.o milkdrop_decor.o: milkdrop_decor.h
milkdrop_gu.o: milkdrop_decor_gu.h
milkdrop_gu.o milkdrop_texture.o: milkdrop_texture.h

# menu_skin.S uses .incbin so the receiver artwork is part of EBOOT.PBP.
menu_skin.o: assets/menu_skin.raw assets/menu_skin_tv.raw
