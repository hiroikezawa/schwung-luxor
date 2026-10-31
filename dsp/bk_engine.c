/* LUXOR - the knob layer over the LXR DRUM voice. See bk_engine.h.
 *
 * GPL-3.0-only (LICENSE), like the LXR port (dsp/lxr) it drives. */
#include "bk_engine.h"

#include <math.h>
#include <string.h>

/* Keys, ranges and defaults. scripts/metadata.py checks this table against
 * module.json's chain_params, so the knob the host shows and the value the
 * DSP uses cannot drift apart. Continuous knobs are 0..1, which the LXR's
 * parser takes as its float path (value.flt, value.itg = v*127). Integer
 * knobs show what the LXR's own display shows: TUNE / FM FREQ are its coarse
 * value - 60 (semitones), FINE / PAN its value - 63 (DTYPE_PM63). */
const bk_param_info bk_params[BP_COUNT] = {
    {"volume",      0.0f,   1.0f,  0.80f, 0},
    {"decay",       0.0f,   1.0f,  0.60f, 0},
    {"p_env",       0.0f,   1.0f,  0.40f, 0},
    {"fm_amount",   0.0f,   1.0f,  0.30f, 0},
    {"mod_decay",   0.0f,   1.0f,  0.45f, 0},
    {"cutoff",      0.0f,   1.0f,  0.80f, 0},
    {"drive",       0.0f,   1.0f,  0.30f, 0},
    {"pitch",       0.0f,  48.0f,  7.0f,  1},   /* scale note index; 7 = C2 in C Phrygian */
    {"root",        0.0f,  11.0f,  0.0f,  1},
    {"scale",       0.0f,  11.0f,  0.0f,  1},
    {"length",      0.0f,   3.0f,  1.0f,  1},
    {"tune",      -60.0f,  67.0f,  0.0f,  1},
    {"fine",      -63.0f,  64.0f,  0.0f,  1},
    {"wave",        0.0f,   5.0f,  0.0f,  1},
    {"pwm",         0.0f,   1.0f,  0.50f, 0},
    {"attack",      0.0f,   1.0f,  0.0f,  0},
    {"a_slope",     0.0f,   1.0f,  0.50f, 0},
    {"p_slope",     0.0f,   1.0f,  0.70f, 0},
    {"fm_freq",   -60.0f,  67.0f, 12.0f,  1},
    {"mod_wave",    0.0f,   5.0f,  0.0f,  1},
    {"fm_mode",     0.0f,   1.0f,  0.0f,  1},
    {"trans",       0.0f,  13.0f,  0.0f,  1},
    {"tr_vol",      0.0f,   1.0f,  0.30f, 0},
    {"tr_freq",     0.0f,   1.0f,  0.50f, 0},
    {"reso",        0.0f,   1.0f,  0.25f, 0},
    {"f_type",      0.0f,   7.0f,  0.0f,  1},
    {"f_drive",     0.0f,   1.0f,  0.20f, 0},
    {"pan",       -63.0f,  64.0f,  0.0f,  1},
    {"s_rate",      0.0f,   1.0f,  1.0f,  0},
    {"lfo_rate",    0.0f,   1.0f,  0.30f, 0},
    {"lfo_sync",    0.0f,  11.0f,  0.0f,  1},
    {"lfo_amt",     0.0f,   1.0f,  0.0f,  0},
    {"lfo_wave",    0.0f,   7.0f,  0.0f,  1},
    {"lfo_retrig",  0.0f,   1.0f,  0.0f,  1},
    {"lfo_phase",   0.0f,   1.0f,  0.0f,  0},
    {"lfo_dest",    0.0f,  29.0f,  0.0f,  1},
    {"fx_mix",      0.0f,   1.0f,  0.35f, 0},
    {"fx_type",     0.0f,   5.0f,  0.0f,  1},
    {"drv_type",    0.0f,   2.0f,  0.0f,  1},
    {"drv_drive",   0.0f,   1.0f,  0.30f, 0},
    {"drv_color",   0.0f,   1.0f,  0.40f, 0},
    {"drv_tone",    0.0f,   1.0f,  0.70f, 0},
    {"drv_vol",     0.0f,   1.0f,  0.70f, 0},
    {"ring_wave",   0.0f,   5.0f,  0.0f,  1},
    {"ring_freq",   0.0f,   1.0f,  0.30f, 0},
    {"cmp_ratio",   0.0f,   5.0f,  3.0f,  1},
    {"cmp_attack",  0.0f,   1.0f,  0.10f, 0},
    {"cmp_decay",   0.0f,   1.0f,  0.30f, 0},
    {"cmp_thresh",  0.0f,   1.0f,  0.60f, 0},
    {"cmp_gain",    0.0f,   1.0f,  0.20f, 0},
    {"dly_time",    0.0f,   1.0f,  0.50f, 0},
    {"dly_range",   0.0f,   1.0f,  0.70f, 0},
    {"dly_tone",    0.0f,   1.0f,  0.70f, 0},
    {"dly_fdbk",    0.0f,   1.0f,  0.40f, 0},
    {"pp_time",     0.0f,   1.0f,  0.50f, 0},
    {"pp_range",    0.0f,   1.0f,  0.70f, 0},
    {"pp_tone",     0.0f,   1.0f,  0.70f, 0},
    {"pp_fdbk",     0.0f,   1.0f,  0.40f, 0},
};

/* ---- scales --------------------------------------------------------------- */
const char *const bk_scale_names[BK_SCALE_COUNT] = {
    "Phrygian", "Minor", "Dorian", "Major", "Mixolyd", "Lydian",
    "Locrian", "HarmMin", "PhrygDom", "MinPent", "In", "Chromatic",
};

/* Bit n = semitone n above the root is in the scale. */
static const uint16_t scale_mask[BK_SCALE_COUNT] = {
    (1u<<0)|(1u<<1)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<10),   /* Phrygian */
    (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<10),   /* Minor (Aeolian) */
    (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<10),   /* Dorian */
    (1u<<0)|(1u<<2)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<11),   /* Major */
    (1u<<0)|(1u<<2)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<10),   /* Mixolydian */
    (1u<<0)|(1u<<2)|(1u<<4)|(1u<<6)|(1u<<7)|(1u<<9)|(1u<<11),   /* Lydian */
    (1u<<0)|(1u<<1)|(1u<<3)|(1u<<5)|(1u<<6)|(1u<<8)|(1u<<10),   /* Locrian */
    (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<11),   /* Harmonic minor */
    (1u<<0)|(1u<<1)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<10),   /* Phrygian dominant */
    (1u<<0)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<10),                   /* Minor pentatonic */
    (1u<<0)|(1u<<1)|(1u<<5)|(1u<<7)|(1u<<8),                    /* In (Miyako-bushi) */
    0x0FFFu,                                                    /* Chromatic */
};

static void clamp_key(int *root, int *scale) {
    *root = ((*root % 12) + 12) % 12;
    if (*scale < 0 || *scale >= BK_SCALE_COUNT) *scale = 0;
}

static int in_scale(int midi, int root, int scale) {
    return (scale_mask[scale] >> ((midi - root + 120) % 12)) & 1u;
}

int bk_pitch_count(int root, int scale) {
    clamp_key(&root, &scale);
    int n = 0;
    for (int m = BK_PITCH_LO; m <= BK_PITCH_HI; ++m) n += in_scale(m, root, scale);
    return n;
}

int bk_pitch_midi(int index, int root, int scale) {
    clamp_key(&root, &scale);
    if (index < 0) index = 0;
    int last = BK_PITCH_LO;
    for (int m = BK_PITCH_LO; m <= BK_PITCH_HI; ++m) {
        if (!in_scale(m, root, scale)) continue;
        if (index-- == 0) return m;
        last = m;
    }
    return last;                                  /* past the top: the top note */
}

int bk_pitch_index_of_midi(float midi, int root, int scale) {
    clamp_key(&root, &scale);
    int best = 0, i = 0;
    float bd = 1e9f;
    for (int m = BK_PITCH_LO; m <= BK_PITCH_HI; ++m) {
        if (!in_scale(m, root, scale)) continue;
        float d = fabsf((float)m - midi);
        if (d < bd - 1e-6f) { bd = d; best = i; }     /* ascending: a tie keeps the lower */
        ++i;
    }
    return best;
}

int bk_quantize_pitch(float semis, int root, int scale) {
    if (!(semis >= 0.0f)) semis = 0.0f;
    if (semis > (float)(BK_PITCH_HI - BK_PITCH_LO)) semis = (float)(BK_PITCH_HI - BK_PITCH_LO);
    root = ((root % 12) + 12) % 12;
    if (scale < 0 || scale >= BK_SCALE_COUNT) scale = 0;
    float target = (float)BK_PITCH_LO + semis;
    int best = -1;
    float best_d = 1e9f;
    for (int n = BK_PITCH_LO; n <= BK_PITCH_HI; ++n) {
        if (!(scale_mask[scale] & (1u << ((n - root + 120) % 12)))) continue;
        float d = fabsf((float)n - target);
        if (d < best_d - 1e-6f) { best_d = d; best = n; }   /* ascending: a tie keeps the lower */
    }
    return best < 0 ? BK_PITCH_LO : best;
}

void bk_note_name(int midi, char *buf, int size) {
    static const char *const names[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    if (!buf || size <= 0) return;
    if (midi < 0) midi = 0;
    if (midi > 127) midi = 127;
    int n = 0;
    const char *s = names[midi % 12];
    while (*s && n < size - 1) buf[n++] = *s++;
    int oct = midi / 12 - 1;                       /* scientific: C1 = 32.7 Hz = MIDI 24 */
    if (n < size - 1) buf[n++] = (char)('0' + (oct < 0 ? 0 : oct));
    buf[n] = 0;
}

/* ---- knob -> LXR parameter ------------------------------------------------- */
static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

/* Hand one value to the LXR, the way its parser takes it. */
static void to_lxr(bk_engine *e, int idx, float v) {
    lxr_drum_t *t = &e->lxr;
    const int i = (int)lrintf(v);
    switch (idx) {
    case BP_VOLUME:    lxr_drum_set(t, LXR_VOL, v); break;
    /* A hit is cut by the next one, so at 1/16s the firmware's own curve spent
     * all but its first fifth on a change nobody could hear. Mostly cubic, but
     * the linear term keeps the bottom moving: the LXR value has 7 bits, and a
     * pure cube is one step for the knob's first 20 %. The top stays the
     * firmware's: long tones are for sparse steps and slow tempos. */
    case BP_DECAY:     lxr_drum_set(t, LXR_DECAY, v * (0.1f + 0.9f * v * v)); break;
    case BP_P_ENV:     lxr_drum_set(t, LXR_MOD_AMOUNT, v); break;
    case BP_FM_AMOUNT: lxr_drum_set(t, LXR_FM_AMOUNT, v); break;
    case BP_MOD_DECAY: lxr_drum_set(t, LXR_PITCH_DECAY, v); break;
    case BP_CUTOFF:    lxr_drum_set(t, LXR_FILTER_FREQ, v); break;
    case BP_DRIVE:     lxr_drum_set(t, LXR_DRIVE, v); break;
    case BP_TUNE:      lxr_drum_set_index(t, LXR_COARSE, i + 60); break;
    case BP_FINE:      lxr_drum_set_index(t, LXR_FINE, i + 63); break;
    case BP_WAVE:      lxr_drum_set_index(t, LXR_OSC_WAVE, i); break;
    case BP_PWM:       lxr_drum_set(t, LXR_PWM, v); break;
    case BP_ATTACK:    lxr_drum_set(t, LXR_ATTACK, v); break;
    case BP_A_SLOPE:   lxr_drum_set(t, LXR_VOL_SLOPE, v); break;
    /* At 127 the firmware's pitch-EG shaper divides by zero (DecayEg_setSlope:
     * 2a/(1-a) with a = 1): the envelope is NaN and the oscillator stops. The
     * knob's top is 126. */
    case BP_P_SLOPE:   lxr_drum_set(t, LXR_PITCH_SLOPE, v * (126.f / 127.f)); break;
    case BP_FM_FREQ:   lxr_drum_set_index(t, LXR_FM_FREQ, i + 60); break;
    case BP_MOD_WAVE:  lxr_drum_set_index(t, LXR_MOD_WAVE, i); break;
    case BP_FM_MODE:   lxr_drum_set_index(t, LXR_MIX_MOD, i); break;
    case BP_TRANS:     lxr_drum_set_index(t, LXR_TRANS_WAVE, i); break;
    case BP_TR_VOL:    lxr_drum_set(t, LXR_TRANS_VOL, v); break;
    case BP_TR_FREQ:   lxr_drum_set(t, LXR_TRANS_FREQ, v); break;
    case BP_RESO:      lxr_drum_set(t, LXR_RESO, v); break;
    case BP_F_TYPE:    lxr_drum_set_index(t, LXR_FILTER_TYPE, i); break;
    case BP_F_DRIVE:   lxr_drum_set(t, LXR_FILTER_DRIVE, v); break;
    case BP_PAN:       lxr_drum_set_index(t, LXR_PAN, i + 63); break;
    case BP_S_RATE:    lxr_drum_set(t, LXR_DECIMATION, v); break;
    case BP_LFO_RATE:  lxr_drum_set(t, LXR_LFO_FREQ, v); break;
    case BP_LFO_SYNC:  lxr_drum_set_index(t, LXR_LFO_SYNC, i); break;
    case BP_LFO_AMT:   lxr_drum_set(t, LXR_LFO_AMOUNT, v); break;
    case BP_LFO_WAVE:  lxr_drum_set_index(t, LXR_LFO_WAVE, i); break;
    case BP_LFO_RETRIG:lxr_drum_set_index(t, LXR_LFO_RETRIG, i); break;
    case BP_LFO_PHASE: lxr_drum_set(t, LXR_LFO_OFFSET, v); break;
    case BP_LFO_DEST:  lxr_drum_set_lfo_target(t, i); break;
    case BP_FX_MIX:    lxr_drum_set_dry_wet(t, v); break;
    case BP_FX_TYPE:   lxr_drum_set_fx_type(t, i); break;
    case BP_DRV_TYPE:  lxr_drum_set_dist_type(t, i); break;
    case BP_DRV_DRIVE: case BP_DRV_COLOR: case BP_DRV_TONE: case BP_DRV_VOL:
        lxr_drum_set_fx_param(t, LXR_FX_DIST, idx - BP_DRV_DRIVE, v); break;
    case BP_RING_WAVE: lxr_drum_set_ring_wave(t, i); break;
    case BP_RING_FREQ: lxr_drum_set_fx_param(t, LXR_FX_RING, 0, v); break;
    case BP_CMP_RATIO: lxr_drum_set_ratio(t, i); break;
    case BP_CMP_ATTACK: case BP_CMP_DECAY: case BP_CMP_THRESH: case BP_CMP_GAIN:
        lxr_drum_set_fx_param(t, LXR_FX_COMP, idx - BP_CMP_ATTACK, v); break;
    case BP_DLY_TIME: case BP_DLY_RANGE: case BP_DLY_TONE: case BP_DLY_FDBK:
        lxr_drum_set_fx_param(t, LXR_FX_DELAY, idx - BP_DLY_TIME, v); break;
    case BP_PP_TIME: case BP_PP_RANGE: case BP_PP_TONE: case BP_PP_FDBK:
        lxr_drum_set_fx_param(t, LXR_FX_PINGPONG, idx - BP_PP_TIME, v); break;
    default: break;      /* PITCH, ROOT, SCALE, LENGTH: LUXOR's own */
    }
}

/* Every value whose effective (override or base) value moved since the LXR
 * last saw it. Once per block: the LXR itself reads parameters per block. */
static void sync_params(bk_engine *e) {
    for (int i = 0; i < BP_COUNT; ++i) {
        float v = bk_engine_value(e, i);
        if (v != e->applied[i]) {
            e->applied[i] = v;
            to_lxr(e, i, v);
        }
    }
}

void bk_engine_init(bk_engine *e, uint32_t seed) {
    memset(e, 0, sizeof(*e));
    lxr_drum_init(&e->lxr, seed);
    for (int i = 0; i < BP_COUNT; ++i) {
        e->param[i] = bk_params[i].def;
        e->ovr[i] = NAN;
        e->applied[i] = NAN;
    }
    sync_params(e);
    e->gain = 0.0f;            /* silent until the first hit */
    e->gain_target = 0.0f;
}

void bk_engine_set_param(bk_engine *e, int idx, float value) {
    if (idx < 0 || idx >= BP_COUNT || !isfinite(value)) return;
    const bk_param_info *p = &bk_params[idx];
    float hi = p->max;
    if (idx == BP_PITCH)                          /* the scale's top note, not 48 */
        hi = (float)(bk_pitch_count((int)e->param[BP_ROOT], (int)e->param[BP_SCALE]) - 1);
    value = clampf(value, p->min, hi);
    if (p->is_int) value = floorf(value + 0.5f);
    e->param[idx] = value;
}

float bk_engine_value(const bk_engine *e, int idx) {
    return isnan(e->ovr[idx]) ? e->param[idx] : e->ovr[idx];
}

void bk_engine_set_override(bk_engine *e, int idx, float value) {
    if (idx < 0 || idx >= BP_COUNT) return;
    if (isnan(value)) { e->ovr[idx] = NAN; return; }
    if (!isfinite(value)) return;
    const bk_param_info *p = &bk_params[idx];
    value = clampf(value, p->min, p->max);
    if (p->is_int) value = floorf(value + 0.5f);
    e->ovr[idx] = value;
}

void bk_engine_set_bpm(bk_engine *e, float bpm) { lxr_drum_set_bpm(&e->lxr, bpm); }

void bk_engine_trigger(bk_engine *e) {
    sync_params(e);            /* the hit plays the values of this instant */
    int note = bk_pitch_midi((int)bk_engine_value(e, BP_PITCH),
                             (int)e->param[BP_ROOT], (int)e->param[BP_SCALE]);
    lxr_drum_trigger(&e->lxr, (uint8_t)note);
    /* at once: a hit is its own attack, and a fade-in would soften the
     * downbeat after every Play */
    e->gain = e->gain_target = 1.0f;
    e->hits++;
}

void bk_engine_release(bk_engine *e) { e->gain_target = 0.0f; }

/* The stop fade: ~25 ms, in 64-frame steps. */
#define BK_FADE_STEP ((float)BK_BLOCK / 1102.5f)
/* One-pole DC blocker, ~3.5 Hz: the LXR's DAC outputs are AC-coupled, and
 * some of its FX (Tube, Fold) sit on a constant offset when idle. */
#define BK_DC_R 0.9995f

void bk_engine_render(bk_engine *e, int16_t *out) {
    sync_params(e);
    lxr_drum_render(&e->lxr, out);
    float g0 = e->gain, g1 = g0;
    if (g0 < e->gain_target) g1 = fminf(e->gain_target, g0 + BK_FADE_STEP);
    else if (g0 > e->gain_target) g1 = fmaxf(e->gain_target, g0 - BK_FADE_STEP);
    e->gain = g1;
    const float step = (g1 - g0) / (float)BK_BLOCK;
    for (int i = 0; i < BK_BLOCK; ++i) {
        const float g = g0 + step * (float)(i + 1);
        for (int c = 0; c < 2; ++c) {
            float x = (float)out[i * 2 + c];
            float y = x - e->dc_x[c] + BK_DC_R * e->dc_y[c];
            if (!isfinite(y)) y = 0.0f;
            e->dc_x[c] = x;
            e->dc_y[c] = y;
            y *= g;
            out[i * 2 + c] = (int16_t)lrintf(clampf(y, -32768.0f, 32767.0f));
        }
    }
}
