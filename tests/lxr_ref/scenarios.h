/*
 * LUXOR -- the test scenes rendered by BOTH the LXR firmware's own code
 * (ref_main.c, unmodified MidiParser.c / DrumVoice.c / lfo.c /
 * modulationNode.c / FxBus.c) and the per-instance port (port_main.c), so
 * that every sample can be compared. GPL-3.0-only (it drives GPL code).
 *
 * A scene is a list of calls through `scene_api`: each harness implements the
 * calls its own way (the reference as the CC messages the LXR's front panel
 * sends, the port through lxr_drum.h). Values are CC values, 0..127, as the
 * firmware's parser receives them.
 */
#ifndef BK_LXR_SCENARIOS_H
#define BK_LXR_SCENARIOS_H

#include "lxr_params.h"
#include <stdint.h>

typedef struct {
    void (*set)(int lxr_param, int cc_value);
    void (*lfo_target)(int lxr_param);           /* -1 = none */
    void (*fx_type)(int lxr_fx);
    void (*fx_param)(int idx, int cc_value);     /* the active FX's P1..P4 */
    void (*dry_wet)(int percent);                /* 0..100 */
    void (*dist_type)(int type);
    void (*ratio)(int ratio);
    void (*ring_wave)(int wave);
    void (*bpm)(float bpm);
} scene_api;

#define SCENE_BLOCKS 1400                        /* 64-frame blocks: 2.03 s */
#define SCENE_COUNT 24

/* Triggers: block numbers. Every scene hits on block 0; `roll` scenes then
 * every 43 blocks (2752 frames ~ a 1/16 at 240 BPM), so retriggers land on a
 * still-sounding voice. */
static inline int scene_triggers(int s, int block, uint8_t *note) {
    const int roll = (s % 3) != 0;
    *note = (uint8_t)(36 + (block / 43) % 12 * 2);
    if (block == 0) return 1;
    return roll && block % 43 == 0 && block < 1100;
}

static void scene_voice_common(const scene_api *a) {
    a->set(LXR_COARSE, 60);
    a->set(LXR_FINE, 63);
    a->set(LXR_VOL, 110);
    a->set(LXR_PAN, 40);
    a->set(LXR_DECIMATION, 127);
    a->set(LXR_DECAY, 75);
    a->set(LXR_PITCH_DECAY, 55);
    a->set(LXR_MOD_AMOUNT, 50);
}

static inline void scene_setup(int s, const scene_api *a) {
    a->bpm(120.f);
    switch (s) {
    case 0:  /* the firmware's power-on voice, untouched */
        break;
    case 1:  /* FM kick: sine carrier, sine modulator, LP */
        scene_voice_common(a);
        a->set(LXR_OSC_WAVE, 0); a->set(LXR_MOD_WAVE, 0); a->set(LXR_MIX_MOD, 0);
        a->set(LXR_FM_AMOUNT, 90); a->set(LXR_FM_FREQ, 79);
        a->set(LXR_FILTER_TYPE, 0); a->set(LXR_FILTER_FREQ, 100); a->set(LXR_RESO, 40);
        a->set(LXR_FILTER_DRIVE, 30); a->set(LXR_DRIVE, 45);
        a->set(LXR_TRANS_WAVE, 0); a->set(LXR_TRANS_VOL, 70); a->set(LXR_TRANS_FREQ, 64);
        a->set(LXR_VOL_SLOPE, 30); a->set(LXR_PITCH_SLOPE, 90);
        break;
    case 2:  /* saw + rec mixed, HP, offset transient */
        scene_voice_common(a);
        a->set(LXR_OSC_WAVE, 2); a->set(LXR_MOD_WAVE, 3); a->set(LXR_MIX_MOD, 1);
        a->set(LXR_FM_AMOUNT, 50); a->set(LXR_FM_FREQ, 67);
        a->set(LXR_FILTER_TYPE, 1); a->set(LXR_FILTER_FREQ, 40); a->set(LXR_RESO, 90);
        a->set(LXR_TRANS_WAVE, 1); a->set(LXR_TRANS_VOL, 40);
        a->set(LXR_ATTACK, 20);
        break;
    case 3:  /* PWM, BP, sample transient */
        scene_voice_common(a);
        a->set(LXR_OSC_WAVE, 5); a->set(LXR_PWM, 33); a->set(LXR_MOD_WAVE, 1); a->set(LXR_MIX_MOD, 0);
        a->set(LXR_FM_AMOUNT, 70); a->set(LXR_FM_FREQ, 48);
        a->set(LXR_FILTER_TYPE, 2); a->set(LXR_FILTER_FREQ, 70); a->set(LXR_RESO, 110);
        a->set(LXR_TRANS_WAVE, 5); a->set(LXR_TRANS_VOL, 127); a->set(LXR_TRANS_FREQ, 20);
        break;
    case 4:  /* noise carrier, unity BP, decimated */
        scene_voice_common(a);
        a->set(LXR_OSC_WAVE, 4); a->set(LXR_MOD_WAVE, 4); a->set(LXR_MIX_MOD, 1);
        a->set(LXR_FILTER_TYPE, 3); a->set(LXR_FILTER_FREQ, 85); a->set(LXR_RESO, 60);
        a->set(LXR_DECIMATION, 50); a->set(LXR_DECAY, 40);
        break;
    case 5:  /* notch, tri FM, sample transients 2..13 cycling via scenes */
        scene_voice_common(a);
        a->set(LXR_OSC_WAVE, 1); a->set(LXR_MOD_WAVE, 2); a->set(LXR_MIX_MOD, 0);
        a->set(LXR_FM_AMOUNT, 127); a->set(LXR_FM_FREQ, 100);
        a->set(LXR_FILTER_TYPE, 4); a->set(LXR_FILTER_FREQ, 60);
        a->set(LXR_TRANS_WAVE, 13); a->set(LXR_TRANS_VOL, 90); a->set(LXR_TRANS_FREQ, 127);
        break;
    case 6:  /* peak and the naive 2-pole LP, max drives */
        scene_voice_common(a);
        a->set(LXR_OSC_WAVE, 0); a->set(LXR_FILTER_TYPE, 6); a->set(LXR_FILTER_FREQ, 30);
        a->set(LXR_FILTER_DRIVE, 127); a->set(LXR_DRIVE, 127); a->set(LXR_MOD_AMOUNT, 127);
        break;
    case 7:  /* every parameter at 0 */
        for (int p = 0; p < LXR_FX_P1; ++p) a->set(p, 0);
        a->set(LXR_DECIMATION, 127); a->set(LXR_VOL, 100);
        break;
    case 8:  /* every parameter at its top */
        for (int p = 0; p < LXR_FX_P1; ++p) {
            int top = 127;
            if (p == LXR_OSC_WAVE || p == LXR_MOD_WAVE) top = LXR_NUM_WAVES - 1;
            if (p == LXR_TRANS_WAVE) top = LXR_NUM_TRANS - 1;
            if (p == LXR_FILTER_TYPE) top = LXR_NUM_FILTERS - 1;
            if (p == LXR_LFO_WAVE) top = LXR_NUM_LFO_WAVES - 1;
            if (p == LXR_LFO_SYNC) top = LXR_NUM_SYNC - 1;
            if (p == LXR_LFO_RETRIG) top = 1;
            if (p == LXR_MIX_MOD) top = 1;
            a->set(p, top);
        }
        break;
    case 9:  /* LFO sine on cutoff */
    case 10: /* LFO tri on coarse */
    case 11: /* LFO noise on FM amount, retriggered */
    case 12: /* LFO rec on filter type (an enum target) */
    case 13: /* LFO exp-down on pan, synced 1/16 at 133 BPM */
    case 14: /* LFO saw-up on the delay time */
    {
        static const int target[] = {LXR_FILTER_FREQ, LXR_COARSE, LXR_FM_AMOUNT,
                                     LXR_FILTER_TYPE, LXR_PAN, LXR_FX_P1};
        static const int wave[] = {0, 1, 5, 4, 7, 2};
        scene_voice_common(a);
        a->set(LXR_OSC_WAVE, 0); a->set(LXR_FILTER_FREQ, 64); a->set(LXR_RESO, 50);
        a->set(LXR_FM_AMOUNT, 60); a->set(LXR_DECAY, 100);
        if (s == 14) {
            a->fx_type(LXR_FX_DELAY); a->dry_wet(60);
            a->fx_param(0, 70); a->fx_param(1, 60); a->fx_param(2, 90); a->fx_param(3, 80);
        }
        if (s == 13) { a->bpm(133.f); a->set(LXR_LFO_SYNC, 10); }
        a->set(LXR_LFO_FREQ, 80); a->set(LXR_LFO_AMOUNT, 70);
        a->set(LXR_LFO_WAVE, wave[s - 9]);
        a->set(LXR_LFO_OFFSET, 30);
        if (s == 11) a->set(LXR_LFO_RETRIG, 1);
        a->lfo_target(target[s - 9]);
        break;
    }
    case 15: case 16: case 17:   /* distortion: tube, fold, clip (RAT) */
        scene_voice_common(a);
        a->set(LXR_OSC_WAVE, 0); a->set(LXR_FM_AMOUNT, 40);
        a->fx_type(LXR_FX_DIST); a->dist_type(s - 15); a->dry_wet(100);
        a->fx_param(0, 90); a->fx_param(1, 60); a->fx_param(2, 80); a->fx_param(3, 100);
        break;
    case 18:  /* ring modulator, sine */
        scene_voice_common(a);
        a->fx_type(LXR_FX_RING); a->ring_wave(0); a->dry_wet(80); a->fx_param(0, 50);
        break;
    case 19:  /* compressor 8:1 */
        scene_voice_common(a);
        a->set(LXR_DRIVE, 80);
        a->fx_type(LXR_FX_COMP); a->ratio(5); a->dry_wet(100);
        a->fx_param(0, 10); a->fx_param(1, 50); a->fx_param(2, 60); a->fx_param(3, 70);
        break;
    case 20:  /* delay, feedback at the top */
    case 21:  /* ping-pong, feedback at the top */
        scene_voice_common(a);
        a->set(LXR_PAN, 10);
        a->fx_type(s == 20 ? LXR_FX_DELAY : LXR_FX_PINGPONG); a->dry_wet(100);
        a->fx_param(0, 100); a->fx_param(1, 127); a->fx_param(2, 127); a->fx_param(3, 127);
        break;
    case 22:  /* ring, rec wave, high freq */
        scene_voice_common(a);
        a->fx_type(LXR_FX_RING); a->ring_wave(3); a->dry_wet(100); a->fx_param(0, 127);
        break;
    case 23:  /* compressor 1:1 with makeup gain at the top */
        scene_voice_common(a);
        a->fx_type(LXR_FX_COMP); a->ratio(0); a->dry_wet(50);
        a->fx_param(0, 127); a->fx_param(1, 127); a->fx_param(2, 0); a->fx_param(3, 127);
        break;
    }
}

#endif
