/*
 * config.h -- the LXR sound engine configuration, for the LUXOR port.
 *
 * Copyright (C) 2012-2026 Erica Synths
 * Modified 2026 by Hiro Ikezawa for LUXOR (Schwung / Ableton Move).
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Every switch the DSP reads is the value of the original
 * Core/Inc/Soundengine/config.h at the vendored commit (third_party/lxr/
 * UPSTREAM_COMMIT). What changed, and why:
 *   - REAL_FS is 44100: the STM32's I2S prescaler runs the LXR at 44140 Hz,
 *     Move runs at exactly 44100, so every rate-dependent constant (phase
 *     increments, LFO rate, compressor and delay times) is computed for the
 *     rate it is actually played at.
 *   - the DTCM/ITCM placement attributes are empty (no such memories here).
 *   - no HAL include, no hardware switches (DAC routing, SD card, bootloader).
 */
#ifndef CONFIG_SOUNDENGINE_H_
#define CONFIG_SOUNDENGINE_H_

#include "stm32f7xx_hal.h"
#include <stdio.h>

#define true 1
#define false 0

#define CALC_SAMPLES_IN_INTERRUPT 1
#define USE_BIG_SAMPLE_BUFFER 1
#define SMOOTH_MIXER_VOL_PAN_DRIVE 1

#if USE_BIG_SAMPLE_BUFFER
#define OUTPUT_DMA_SIZE 64
#else
#define OUTPUT_DMA_SIZE 32
#endif
#define DMA_MASK ((OUTPUT_DMA_SIZE*2)-1)
#define UNIT_GAIN_DRIVE 0
#define SET_PARAM_ARRAY_IN_PARSER 1
#define USE_FILTER_DRIVE 0
#define CALC_TONE_CONTROL 0
#define AMP_EG_SYNC 0
#define ENABLE_MIX_OSC 1
#define ENABLE_DRUM_SVF 1

#define EG_SPEED 	1;
#define PITCH_AMOUNT_FACTOR 32

#define INTERPOLATE_OSC 1
#define INTERPOLATE_FM_OSC 1

#ifndef REAL_FS
#define REAL_FS				(float)44100.f
#endif
#define REAL_FS2			(REAL_FS/2.f)

#define NUM_DRUM_VOICES 3

#define INDTCM
#define INDTCMZ
#define IN_ITCM_RAM

#endif /* CONFIG_SOUNDENGINE_H_ */
