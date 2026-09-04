#pragma once

/* Defaults written to PSPStreamer.cfg on first settings change. */
#define PSP_STREAMER_HOST "psp-streamer.local"
#define PSP_STREAMER_PORT 8091

/* 0 automatically selects the first saved PSP infrastructure profile. */
#define PSP_NETWORK_PROFILE 0

/* Diagnostic profile: lowers total H.264+PCM traffic by about one third. */
#define PSP_STREAMER_PROFILE "low"
#define PSP_AUDIO_SAMPLE_RATE 44100

/* Install EBOOT.PBP and cooleyesBridge.prx in this one folder. */
#define PSP_STREAMER_INSTALL_DIR "ms0:/PSP/GAME/PSPSTREAMER/"
