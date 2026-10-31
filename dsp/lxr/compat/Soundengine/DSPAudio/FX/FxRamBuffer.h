/* LUXOR LXR port: the FX delay memory, per instance.
 *
 * Copyright (C) 2012-2026 Erica Synths (sizes and comments of the original
 * Core/Inc/Soundengine/DSPAudio/FX/FxRamBuffer.h). GPL-3.0-only.
 *
 * The firmware has ONE global FX_BUFFER; a Move can run a LUXOR in each of
 * four slots, so every delay owns its own. Delay.c is compiled unmodified: its
 * every FX_BUFFER access sits in a function whose Delay is called `d`, and the
 * memory is laid out directly behind that struct (lxr_delay_unit_t in
 * lxr_drum.h, which static-asserts the layout). */
#ifndef INC_SOUNDENGINE_DSPAUDIO_FX_FXRAMBUFFER_H_
#define INC_SOUNDENGINE_DSPAUDIO_FX_FXRAMBUFFER_H_
#include "Soundengine/globals.h"
#include "Soundengine/config.h"
//32768 * 2 = 65536 byte = 64kb => 0,74	sec mono delay or 0,37	sec stereo delay
#define FX_BUFFER_SIZE 0x7FFF//32768 //must be power of to for masking!
#define FX_BUFFER ((__fp16 *)(void *)((d) + 1))
#define MAX_ALLPASS_SIZE 0x800
#endif
