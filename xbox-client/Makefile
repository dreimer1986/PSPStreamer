XBE_TITLE = PSPStreamer\ Xbox\ Player\ Preview
NXDK_DIR ?= $(abspath ../.toolchain/nxdk)
NXDK_NET = y
NXDK_SDL = y
SRCS = $(CURDIR)/player_main.c
CFLAGS += -O2 -Wall -Wextra
CFLAGS += -I$(CURDIR)/../psp-client -I$(CURDIR)/shared
LDFLAGS += -map:$(CURDIR)/bin/player.map
include $(NXDK_DIR)/Makefile
