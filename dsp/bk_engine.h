/* LUXOR - the knob layer over the LXR DRUM voice and FX bus.
 *
 * The sound is the LXR-02 firmware's own DRUM voice and FX (dsp/lxr and
 * third_party/lxr, GPL-3.0-only); this file holds what LUXOR adds around
 * it: the knobs and their ranges, the motion overrides, the
 * PITCH-as-a-scale-note rule, the transport stop fade and the output stage.
 *
 *   PITCH (scale note) --> Drum_trigger(note) ; TUNE/FINE = the LXR's coarse/fine
 *   every knob ---------> the LXR parameter it names (lxr_drum_set*)
 *   LXR voice --> LXR FX bus (one FX at a time, MIX = its dry/wet)
 *     --> DC blocker (the LXR's outputs are AC-coupled) --> stop fade --> out
 *
 * The LXR renders 64-frame blocks and reads its parameters once per block;
 * the plugin calls bk_engine_render() per block and lands each hit on the
 * nearest block edge (+-0.7 ms), as the firmware's own triggers land.
 *
 * Every function here is called from the SPI audio callback: no allocation,
 * no I/O, bounded work. */
#ifndef BK_ENGINE_H
#define BK_ENGINE_H

#include <stdint.h>
#include "lxr/lxr_drum.h"

#define BK_SR 44100.0f
#define BK_BLOCK LXR_BLOCK           /* 64 */

enum {
    /* main page, knobs 1-8 */
    BP_VOLUME, BP_DECAY, BP_P_ENV, BP_FM_AMOUNT, BP_MOD_DECAY, BP_CUTOFF, BP_DRIVE, BP_PITCH,
    /* Scale + Motion (plugin-level). Recording is armed by the REC PAD,
     * which is runtime state (bk_motion.write), not a parameter. */
    BP_ROOT, BP_SCALE, BP_LENGTH,
    /* Osc: the LXR's Oscillator, Amp EG and Modulation pages */
    BP_TUNE, BP_FINE, BP_WAVE, BP_PWM, BP_ATTACK, BP_A_SLOPE, BP_P_SLOPE,
    /* FM: FM and Transient pages */
    BP_FM_FREQ, BP_MOD_WAVE, BP_FM_MODE, BP_TRANS, BP_TR_VOL, BP_TR_FREQ,
    /* Filter: Filter page + the Mix page's pan and sample rate */
    BP_RESO, BP_F_TYPE, BP_F_DRIVE, BP_PAN, BP_S_RATE,
    /* LFO */
    BP_LFO_RATE, BP_LFO_SYNC, BP_LFO_AMT, BP_LFO_WAVE, BP_LFO_RETRIG, BP_LFO_PHASE, BP_LFO_DEST,
    /* FX bus */
    BP_FX_MIX, BP_FX_TYPE,
    BP_DRV_TYPE, BP_DRV_DRIVE, BP_DRV_COLOR, BP_DRV_TONE, BP_DRV_VOL,
    BP_RING_WAVE, BP_RING_FREQ,
    BP_CMP_RATIO, BP_CMP_ATTACK, BP_CMP_DECAY, BP_CMP_THRESH, BP_CMP_GAIN,
    BP_DLY_TIME, BP_DLY_RANGE, BP_DLY_TONE, BP_DLY_FDBK,
    BP_PP_TIME, BP_PP_RANGE, BP_PP_TONE, BP_PP_FDBK,
    BP_COUNT
};

typedef struct {
    const char *key;
    float min, max, def;
    int is_int;              /* int / enum: integral values only */
} bk_param_info;

extern const bk_param_info bk_params[BP_COUNT];

/* ---- pitch: the knob IS a note of the scale ---------------------------------
 * PITCH is an integer INDEX into the notes of (ROOT, SCALE) between MIDI 24
 * (C1) and 72 (C5): every detent is the next scale note, nothing is quantized
 * afterwards (29 notes in C Phrygian, 49 chromatic, so BP_PITCH's table max
 * is 48 and the served chain_params carry the current scale's top). The note
 * is what the LXR voice is triggered with: TUNE 0 plays it at its pitch. */
#define BK_PITCH_LO 24
#define BK_PITCH_HI 72
#define BK_SCALE_COUNT 12
extern const char *const bk_scale_names[BK_SCALE_COUNT];
int  bk_pitch_count(int root, int scale);                 /* notes in C1..C5 */
int  bk_pitch_midi(int index, int root, int scale);       /* clamps the index */
int  bk_pitch_index_of_midi(float midi, int root, int scale);   /* nearest; tie goes down */
/* Nearest note of (root, scale) to BK_PITCH_LO + semis; ties go down. Only
 * used to read v1/v2 states, whose PITCH was a semitone position. */
int  bk_quantize_pitch(float semis, int root, int scale);
/* "C1", "Db2", ... (flats, as Move's own UI). buf >= 5. */
void bk_note_name(int midi, char *buf, int size);

/* FX TYPE values (BP_FX_TYPE) are lxr_params.h's LXR_FX_*. */

typedef struct {
    float param[BP_COUNT];   /* BASE values: what the knobs set and state saves */
    float ovr[BP_COUNT];     /* motion playback override, NAN = none */
    float applied[BP_COUNT]; /* what the LXR was last given (NAN = never) */

    lxr_drum_t lxr;

    float gain, gain_target; /* stop fade */
    float dc_x[2], dc_y[2];  /* output DC blocker */
    int hits;                /* diagnostics / tests */
} bk_engine;

void  bk_engine_init(bk_engine *e, uint32_t seed);
void  bk_engine_set_param(bk_engine *e, int idx, float value);   /* clamps */
float bk_engine_value(const bk_engine *e, int idx);              /* override or base */
void  bk_engine_set_override(bk_engine *e, int idx, float value);/* NAN clears; clamps */
void  bk_engine_set_bpm(bk_engine *e, float bpm);                /* LFO sync */
/* A 1/16 hit at the current PITCH (and every other value): Drum_trigger. */
void  bk_engine_trigger(bk_engine *e);
/* Transport stopped: ~25 ms fade of the whole output, then silence (the FX
 * keep running underneath). Cancelled by the next hit. */
void  bk_engine_release(bk_engine *e);
/* One LXR block: BK_BLOCK interleaved stereo frames. */
void  bk_engine_render(bk_engine *e, int16_t *out_lr);

#endif
