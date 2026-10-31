/* LUXOR - touch automation. GPL-3.0-only (LICENSE). See bk_motion.h. */
#include "bk_motion.h"

#include <math.h>

/* Lane order is the bit order of every mask the UI reads; new lanes go at the
 * END (saved lanes are keyed by name, so this never breaks a state). A lane
 * records from whichever page its knob is on. Knobs 2-8 of the main page;
 * any parameter can become a lane by being added here (and to MOTION_KEYS in
 * ui_core.mjs) -- the FX page's knobs included. */
const int bk_motion_param[BK_MOTION_LANES] = {
    BP_DECAY, BP_P_ENV, BP_FM_AMOUNT, BP_MOD_DECAY, BP_CUTOFF, BP_DRIVE, BP_PITCH,
};
/* Interpolated between points (the rest hold a point's value: the decays,
 * the pitch envelope depth and the pitch act per hit). */
static const int lane_continuous[BK_MOTION_LANES] = {0, 0, 1, 0, 1, 1, 0};

const int bk_length_steps[4] = {8, 16, 32, 64};

void bk_motion_init(bk_motion *m) {
    for (int k = 0; k < BK_MOTION_LANES; ++k) {
        for (int i = 0; i < BK_POINTS; ++i) m->pt[k][i] = BK_EMPTY;
        m->last[k] = -1;
        m->age[k] = 0;
    }
    m->touched = 0;
    m->point = -1;
    m->step = -1;
    m->write = 0;
}

int bk_motion_lane(int param_idx) {
    for (int k = 0; k < BK_MOTION_LANES; ++k) if (bk_motion_param[k] == param_idx) return k;
    return -1;
}

int bk_motion_len_steps(const bk_engine *e) {
    int i = (int)e->param[BP_LENGTH];
    return bk_length_steps[i < 0 ? 0 : (i > 3 ? 3 : i)];
}

static int rec_on(const bk_motion *m) { return m->write; }

void bk_motion_touch(bk_motion *m, bk_engine *e, int k, int down) {
    if (k < 0 || k >= BK_MOTION_LANES) return;
    int p = bk_motion_param[k];
    m->age[k] = 0;
    if (down && (m->touched & (1u << k))) return;      /* heartbeat */
    m->last[k] = -1;
    if (!down) { m->touched &= ~(1u << k); return; }
    m->touched |= 1u << k;
    if (!rec_on(m)) return;                      /* REC pad off: read-only, touch is ignored */
    /* Pick up what is PLAYING, so taking hold of an automated knob does not
     * make the sound jump to wherever the knob was left; the turn continues
     * from there. Then the hand owns the parameter. */
    if (!isnan(e->ovr[p])) e->param[p] = e->ovr[p];
    bk_engine_set_override(e, p, NAN);
    if (m->point >= 0) {
        m->pt[k][m->point] = bk_motion_q(bk_motion_norm(p, e->param[p]));
        m->last[k] = m->point;
    }
}

void bk_motion_age(bk_motion *m, int frames) {
    for (int k = 0; k < BK_MOTION_LANES; ++k) {
        if (!(m->touched & (1u << k))) continue;
        if ((m->age[k] += frames) > BK_TOUCH_TIMEOUT_SAMPLES) {
            m->touched &= ~(1u << k);
            m->last[k] = -1;
        }
    }
}

static void playback(bk_motion *m, bk_engine *e, int k, int ip, int npts, float frac) {
    int p = bk_motion_param[k];
    uint16_t a = m->pt[k][ip];
    if (a == BK_EMPTY) { bk_engine_set_override(e, p, NAN); return; }
    float v = bk_motion_dq(a);
    if (lane_continuous[k]) {
        uint16_t b = m->pt[k][ip + 1 == npts ? 0 : ip + 1];
        if (b != BK_EMPTY) v += (bk_motion_dq(b) - v) * frac;
    }
    bk_engine_set_override(e, p, bk_motion_denorm(p, v));
}

void bk_motion_update(bk_motion *m, bk_engine *e, double pos16) {
    if (!(pos16 >= 0.0)) {                       /* stopped: the knobs play */
        if (m->point >= 0 || m->step >= 0)
            for (int k = 0; k < BK_MOTION_LANES; ++k) bk_engine_set_override(e, bk_motion_param[k], NAN);
        for (int k = 0; k < BK_MOTION_LANES; ++k) m->last[k] = -1;
        m->point = m->step = -1;
        return;
    }
    int len = bk_motion_len_steps(e);
    int npts = len * BK_SUB;
    double P = pos16 * BK_SUB;
    double fl = floor(P + 1e-9);
    float frac = (float)(P - fl);
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    int64_t ip64 = (int64_t)fl;
    int ip = (int)(((ip64 % npts) + npts) % npts);
    int rec = rec_on(m);

    for (int k = 0; k < BK_MOTION_LANES; ++k) {
        if (rec && (m->touched & (1u << k))) {
            int p = bk_motion_param[k];
            bk_engine_set_override(e, p, NAN);
            uint16_t q = bk_motion_q(bk_motion_norm(p, e->param[p]));
            /* Every point ENTERED since the last update takes the hand's
             * value (a skipped block must not leave holes under a finger);
             * the current point is not rewritten while we are inside it, so a
             * point holds the value at its START -- which is what makes linear
             * playback retrace the hand. */
            int j = m->last[k];
            /* host beat jitter can step back a hair across a point edge:
             * that is not a new point, and walking "forward" to it would
             * rewrite the whole loop */
            if (j >= 0 && j < npts) {
                int back = (j - ip + npts) % npts;
                if (back > 0 && back < BK_SUB) continue;
            }
            if (j < 0 || j >= npts) m->pt[k][ip] = q;
            else for (int n = 0; j != ip && n < npts; ++n) {
                j = j + 1 == npts ? 0 : j + 1;
                m->pt[k][j] = q;
            }
            m->last[k] = ip;
        } else {
            playback(m, e, k, ip, npts, frac);
        }
    }
    m->point = ip;
    m->step = ip / BK_SUB;
}

void bk_motion_clear(bk_motion *m, bk_engine *e, int lane) {
    for (int k = 0; k < BK_MOTION_LANES; ++k) {
        if (lane >= 0 && k != lane) continue;
        for (int i = 0; i < BK_POINTS; ++i) m->pt[k][i] = BK_EMPTY;
        m->last[k] = -1;
        bk_engine_set_override(e, bk_motion_param[k], NAN);
    }
}

unsigned bk_motion_mask(const bk_motion *m) {
    unsigned mask = 0;
    for (int k = 0; k < BK_MOTION_LANES; ++k)
        for (int i = 0; i < BK_POINTS; ++i)
            if (m->pt[k][i] != BK_EMPTY) { mask |= 1u << k; break; }
    return mask;
}

unsigned bk_motion_recording(const bk_motion *m, const bk_engine *e) {
    (void)e;
    return (rec_on(m) && m->point >= 0) ? m->touched : 0u;
}
