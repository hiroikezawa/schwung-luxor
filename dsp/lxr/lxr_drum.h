/*
 * lxr_drum.h -- one LXR DRUM voice and the LXR FX bus, per instance.
 *
 * Copyright (C) 2012-2026 Erica Synths
 * Modified 2026 by Hiro Ikezawa for LUXOR (Schwung / Ableton Move).
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * The LXR firmware runs three DRUM voices out of global arrays
 * (voiceArray[], velocityModulators[], fxBus, FX_BUFFER) and changes a sound
 * by feeding MIDI CCs through midiParser_ccHandler(). This file is that code
 * with the globals turned into ONE instance -- a Move runs a LUXOR in up
 * to four slots at once, each with its own voice and its own delay memory:
 *
 *   lxr_drum.c  Drum_trigger / calcDrumVoiceAsync / calcDrumVoiceSyncBlock
 *               (DrumVoice.c), the LFO (lfo.c), the modulation node
 *               (modulationNode.c), the DRUM 1 and FX cases of
 *               midiParser_ccHandler (MidiParser.c), one voice's slice of
 *               mixer_calcNextSampleBlock (mixer.c) and FxBus_process /
 *               FxBus_updateParameter (FxBus.c) -- each modified only where it
 *               named a global; see the comments at each function.
 *
 * Everything else the voice runs -- oscillators and wavetables, the ZDF SVF,
 * the envelopes, transient generator, snap EG, voice distortion, and the four
 * FX (Distortion, Ringmod, Compressor, Delay) -- is the vendored source in
 * third_party/lxr, compiled unmodified.
 *
 * Sample rate 44100 Hz (config.h), blocks of OUTPUT_DMA_SIZE = 64 frames, as
 * the firmware renders them. All calls come from the audio callback: no
 * allocation, no I/O.
 */
#ifndef BK_LXR_DRUM_H
#define BK_LXR_DRUM_H

#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/FX/Distortion.h"
#include "Soundengine/DSPAudio/FX/Ringmod.h"
#include "Soundengine/DSPAudio/FX/Compressor.h"
#include "Soundengine/DSPAudio/FX/Delay.h"
#include <stddef.h>
#include "lxr_params.h"

#define LXR_BLOCK OUTPUT_DMA_SIZE

/* LFO destinations. The firmware's modTargets[] for DRUM 1, minus what has
 * no meaning here: velocity (LUXOR's steps carry none), the LFO's own
 * settings and the voice/target selectors. Index 0 = none. */
extern const int  lxr_lfo_targets[];         /* -> LXR_* parameter */
extern const int  lxr_lfo_target_count;      /* including "none" */

/* A Delay with its memory directly behind it: compat FxRamBuffer.h addresses
 * FX_BUFFER as (d + 1). */
typedef struct {
    Delay d;
    __fp16 ram[FX_BUFFER_SIZE + 1];
} lxr_delay_unit_t;
_Static_assert(offsetof(lxr_delay_unit_t, ram) == sizeof(Delay), "FX_BUFFER must follow its Delay");

typedef struct {
    DrumVoice v;
    float param[LXR_PARAM_COUNT];   /* base values, 0..1 (= the firmware's activeParameters/127) */
    /* How each base value was set: a CC value 0..127 (the firmware's only
     * kind, and what modNode_resetTargets puts back through the parser's
     * integer path), or -1 for a continuous knob value (the parser's float
     * path, which is how LUXOR's knobs and automation set it). */
    int16_t param_cc[LXR_PARAM_COUNT];
    int   lfo_target;               /* index into lxr_lfo_targets */

    /* mixer.c, this voice's share */
    float dec_cnt;
    int16_t dec_sample;

    /* FxBus.c */
    int   fx_type;
    float dry_wet;
    float fx_par[LXR_FX_TYPES][4];
    MasterDistortion dist;
    Ringmod ring;
    Compressor comp;
    lxr_delay_unit_t delay[2];      /* [0] DELAY_NORMAL, [1] DELAY_PINGPONG */
    /* An FX change crossfades the outgoing and incoming FX over a few ms
     * (an abrupt switch is a click). */
    int   fx_prev;
    int   fx_fade;                  /* frames left, 0 = none */

    uint32_t rng;                   /* GetRngValue(): the STM32's hardware RNG */
    float bpm;                      /* seq_getBpm() */
} lxr_drum_t;

#define LXR_FX_FADE_FRAMES 441      /* 10 ms */

void lxr_drum_init(lxr_drum_t *t, uint32_t seed);
/* Base value of a sound parameter, 0..1 (enums: index / 127, as the firmware
 * stores them). Applied at once, the way a CC is. */
void lxr_drum_set(lxr_drum_t *t, int param, float v);
void lxr_drum_set_index(lxr_drum_t *t, int param, int index);   /* enums */
void lxr_drum_set_lfo_target(lxr_drum_t *t, int index);
void lxr_drum_set_fx_type(lxr_drum_t *t, int type);
void lxr_drum_set_fx_param(lxr_drum_t *t, int type, int idx, float v);     /* 0..1 */
void lxr_drum_set_dry_wet(lxr_drum_t *t, float v);                         /* 0..1 */
void lxr_drum_set_dist_type(lxr_drum_t *t, int type);   /* tub fld clp */
void lxr_drum_set_ratio(lxr_drum_t *t, int ratio);      /* 1:1 .. 8:1 */
void lxr_drum_set_ring_wave(lxr_drum_t *t, int wave);
void lxr_drum_set_bpm(lxr_drum_t *t, float bpm);
/* A hit at full velocity on MIDI note `note` (the firmware's Drum_trigger). */
void lxr_drum_trigger(lxr_drum_t *t, uint8_t note);
/* One 64-frame block, out = interleaved stereo int16. */
void lxr_drum_render(lxr_drum_t *t, int16_t *out);

#endif
