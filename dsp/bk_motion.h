/* LUXOR - touch automation for the main page's sound knobs. GPL-3.0-only (LICENSE).
 *
 * Grown from FMDRUM's fmd_motion (8 values per knob, one per 1/16): the same
 * contract -- a knob's capacitive TOUCH is the write window, playback is an
 * engine OVERRIDE and never a write to the knob's base value -- at 8 points
 * per 1/16, so a continuous sweep records as a curve, not a staircase.
 *
 *   touch down  (transport running, REC pad on)  -> writing starts: the
 *               override is dropped (you hear the hand) and the point under
 *               the playhead takes the knob's value;
 *   held        every point the playhead ENTERS takes the knob's value at that
 *               moment, turning or not -- touch time is write time;
 *   touch up    writing stops; the lane plays back from the next update.
 *
 * Nothing is ever written while untouched. A point that was never written
 * plays the knob's own value, so a lane can hold automation for part of the
 * loop only.
 *
 * Lanes: knobs 2-8 of the main page (DECAY, P.ENV, FM AMT, MOD DEC, CUTOFF,
 * DRIVE, PITCH); VOLUME is never recorded.
 *
 * Playback: continuous knobs (FM AMT, CUTOFF, DRIVE) interpolate linearly
 * between points; stepped ones (DECAY, P.ENV, MOD DEC, PITCH) hold a point's
 * value, and PITCH is only ever read at a hit, where the point is the step's
 * first -- the value at the start of the 1/16.
 *
 * All calls come from the audio callback: no allocation. */
#ifndef BK_MOTION_H
#define BK_MOTION_H

#include "bk_engine.h"
#include <stdint.h>

#define BK_MOTION_LANES 7
#define BK_MAX_STEPS 64
#define BK_SUB 8                                   /* points per 1/16 */
#define BK_POINTS (BK_MAX_STEPS * BK_SUB)
#define BK_EMPTY 0xFFFFu
#define BK_QMAX 4094                               /* 12-bit values 0..4094 */
/* A touch the UI stops restating (screen closed, UI gone) ends on its own:
 * ui_core.mjs restates held touches every 150 ms (PISTON's heartbeat). */
#define BK_TOUCH_TIMEOUT_SAMPLES 22050             /* 0.5 s */

/* Lane k -> engine parameter; bit k of every mask. */
extern const int bk_motion_param[BK_MOTION_LANES];
extern const int bk_length_steps[4];               /* LENGTH enum -> steps */

typedef struct {
    uint16_t pt[BK_MOTION_LANES][BK_POINTS];
    unsigned touched;                              /* bit per lane: finger on it */
    int last[BK_MOTION_LANES];                     /* last point written, -1 none */
    int age[BK_MOTION_LANES];                      /* samples since the touch was stated */
    int point;                                     /* playhead point, -1 stopped */
    int step;                                      /* playhead 1/16 in the loop, -1 stopped */
    int write;                                     /* REC pad: touches record only while on.
                                                    * Off at load and never saved, so a set
                                                    * cannot come back armed. */
} bk_motion;

void bk_motion_init(bk_motion *m);
int  bk_motion_lane(int param_idx);                /* -1: not an automated knob */
int  bk_motion_len_steps(const bk_engine *e);
/* down = 1 again on a held lane only restates it (heartbeat). */
void bk_motion_touch(bk_motion *m, bk_engine *e, int lane, int down);
/* Once per rendered block: lets go of touches nobody restated. */
void bk_motion_age(bk_motion *m, int frames);
/* pos16 = 1/16 position since the transport's start (beat x 4), < 0 stopped.
 * Records under a finger, else sets the playback overrides. */
void bk_motion_update(bk_motion *m, bk_engine *e, double pos16);
void bk_motion_clear(bk_motion *m, bk_engine *e, int lane);   /* -1 = all */
unsigned bk_motion_mask(const bk_motion *m);                  /* bit per lane with data */
unsigned bk_motion_recording(const bk_motion *m, const bk_engine *e);

/* Points are stored NORMALIZED over the parameter's own range (PITCH is
 * 0..48), so the state format does not care about units. */
static inline float bk_motion_norm(int p, float v) {
    return (v - bk_params[p].min) / (bk_params[p].max - bk_params[p].min);
}
static inline float bk_motion_denorm(int p, float n) {
    float v = bk_params[p].min + n * (bk_params[p].max - bk_params[p].min);
    return bk_params[p].is_int ? (float)(int)(v + 0.5f) : v;
}
static inline uint16_t bk_motion_q(float v01) {
    if (!(v01 >= 0.0f)) v01 = 0.0f;
    if (v01 > 1.0f) v01 = 1.0f;
    return (uint16_t)(v01 * (float)BK_QMAX + 0.5f);
}
static inline float bk_motion_dq(uint16_t q) { return (float)q / (float)BK_QMAX; }

#endif
