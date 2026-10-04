XBE_TITLE = PSPStreamer\ Xbox\ Player\ Preview
NXDK_DIR ?= $(abspath ../.toolchain/nxdk)
NXDK_NET = y
NXDK_SDL = y
SRCS = $(CURDIR)/player_main.c $(CURDIR)/video_backend.c
SRCS += $(wildcard $(CURDIR)/vendor/libmpeg2/*.c)
LTO = y
CFLAGS += -O3 -Wall -Wextra -I$(CURDIR)/vendor/libmpeg2
CFLAGS += -I$(CURDIR)/../psp-client -I$(CURDIR)/shared
CFLAGS += -I$(NXDK_DIR)/lib/sdl/SDL2/src/video/xbox
LDFLAGS += -map:$(CURDIR)/bin/player.map
include $(NXDK_DIR)/Makefile
