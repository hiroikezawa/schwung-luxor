/*
 * lxr_params.h -- the LXR DRUM voice's parameters and FX bus types, as
 * LUXOR addresses them. No includes, so the reference renderer
 * (tests/lxr_ref) can share it with the port.
 *
 * Copyright (C) 2012-2026 Erica Synths
 * Modified 2026 by Hiro Ikezawa for LUXOR (Schwung / Ableton Move).
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef BK_LXR_PARAMS_H
#define BK_LXR_PARAMS_H

/* The DRUM voice's sound parameters, in the order of the LXR's own menu
 * pages (menuPages.h, "Voice 1"): Oscillator, Amp EG, Modulation, FM,
 * Transient, Filter, LFO, Mix. Values are what the firmware's parser takes:
 * 0..127 (DTYPE_0B127 / DTYPE_PM63, stored as v/127) or a menu index. */
enum {
    LXR_COARSE, LXR_FINE, LXR_OSC_WAVE, LXR_PWM,
    LXR_ATTACK, LXR_DECAY, LXR_VOL_SLOPE,
    LXR_PITCH_DECAY, LXR_PITCH_SLOPE, LXR_MOD_AMOUNT,
    LXR_FM_AMOUNT, LXR_FM_FREQ, LXR_MOD_WAVE, LXR_MIX_MOD,
    LXR_TRANS_WAVE, LXR_TRANS_VOL, LXR_TRANS_FREQ,
    LXR_FILTER_FREQ, LXR_RESO, LXR_FILTER_TYPE, LXR_FILTER_DRIVE,
    LXR_LFO_FREQ, LXR_LFO_SYNC, LXR_LFO_AMOUNT, LXR_LFO_WAVE, LXR_LFO_RETRIG, LXR_LFO_OFFSET,
    LXR_VOL, LXR_PAN, LXR_DECIMATION, LXR_DRIVE,
    LXR_FX_P1, LXR_FX_P2, LXR_FX_P3, LXR_FX_P4,      /* the active FX's four */
    LXR_PARAM_COUNT
};

/* Menu sizes (MenuText.h). */
#define LXR_NUM_WAVES       6       /* Sin Tri Saw Rec Noi PWM */
#define LXR_NUM_TRANS       14      /* Snp Ofs + 12 samples */
#define LXR_NUM_FILTERS     8       /* LP HP BP UBP Nch Pek LP2 off */
#define LXR_NUM_LFO_WAVES   8
#define LXR_NUM_SYNC        12
#define LXR_NUM_RETRIG      2       /* off, this voice (the firmware's "v1") */

/* The FX bus types, as FxBus.h's enum, with the delay's two modes listed
 * apart -- each delay mode has its own delay memory here (see lxr_drum.c). */
enum { LXR_FX_OFF, LXR_FX_DIST, LXR_FX_RING, LXR_FX_COMP, LXR_FX_DELAY, LXR_FX_PINGPONG,
       LXR_FX_TYPES };

#endif
