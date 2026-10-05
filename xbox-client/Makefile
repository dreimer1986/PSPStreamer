XBE_TITLE = PSPStreamer\ Xbox\ Player\ Preview
NXDK_DIR ?= $(abspath ../.toolchain/nxdk)
NXDK_NET = y
NXDK_SDL = y
VISUAL_ROOT := $(if $(wildcard $(CURDIR)/../psp-client/milkdrop_gu.c),$(CURDIR)/../psp-client,$(CURDIR)/shared)
SRCS = $(CURDIR)/player_main.c $(CURDIR)/video_backend.c
SRCS += $(wildcard $(CURDIR)/vendor/libmpeg2/*.c)
SRCS += $(CURDIR)/visual_gpu.c $(CURDIR)/visual_pbkit.c $(CURDIR)/visual_math.c
SRCS += $(CURDIR)/generated/visual_effects.c $(CURDIR)/generated/visual_logo.c
VISUAL_SHARED = milkdrop_warp milkdrop_preset preset_math milkdrop_signal milkdrop_wave milkdrop_wave_extra milkdrop_decor milkdrop_texture cave_visual cave_paths
# Separate Xbox object files: never overwrite PSP SDK objects in psp-client/.
SRCS += $(addprefix $(CURDIR)/generated/,$(addsuffix .c,$(VISUAL_SHARED)))
SHADER_OBJS = visual_vertex.inl visual_texture.inl visual_plain.inl visual_rgb.inl
LTO = y
CFLAGS += -O3 -Wall -Wextra -I$(CURDIR)/vendor/libmpeg2
CFLAGS += -I$(VISUAL_ROOT) -I$(CURDIR)/shared
CFLAGS += -I$(NXDK_DIR)/lib/sdl/SDL2/src/video/xbox
CFLAGS += -I$(CURDIR) -I$(NXDK_DIR)/lib/pbkit
CFLAGS += -Dstrcasecmp=_stricmp
CFLAGS += -include $(CURDIR)/visual_portability.h
LDFLAGS += -map:$(CURDIR)/bin/player.map
include $(NXDK_DIR)/Makefile

# The pinned SDK's fp20 compiler emits signed left shifts for register masks.
# Keep generated register expressions well-defined under O3/LTO and UBSan.
visual_texture.inl visual_rgb.inl visual_plain.inl: %.inl: %.ps.cg $(FP20COMPILER) $(CURDIR)/tools/visual_shader.py
	python3 $(CURDIR)/tools/visual_shader.py $(CGC) $(FP20COMPILER) $< $@

$(CURDIR)/generated/visual_effects.c $(CURDIR)/generated/visual_logo.c &: $(CURDIR)/tools/visual_adapter.py $(wildcard $(VISUAL_ROOT)/*.[ch]) $(VISUAL_ROOT)/assets/monkey-flight-logo.raw
	python3 $(CURDIR)/tools/visual_adapter.py

$(CURDIR)/generated/%.c: $(VISUAL_ROOT)/%.c
	mkdir -p $(CURDIR)/generated
	cp $< $@
