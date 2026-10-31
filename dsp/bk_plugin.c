/* LUXOR - Schwung plugin API v2 adapter. GPL-3.0-only (LICENSE).
 *
 * EVERY entry point here runs on the SPI audio callback (plugin_api_v1.h), so:
 * no allocation (instances come from a static pool), no file I/O, no logging,
 * bounded work.
 *
 * There is no note input: while Move's transport runs the voice fires on
 * every 1/16 by itself (host get_beat_position, MIDI clock fallback), and it
 * is silent while the transport is stopped. MIDI notes are ignored.
 *
 * The pads (ui_core.mjs, taken while the screen is up) talk to it through
 * three keys: "write" (the REC pad arms motion recording), "gate_toggle"
 * (the 16 trigger steps) and "randomize" (a Phrase Gen phrase into PITCH). */
#include "host/plugin_api_v1.h"
#include "bk_engine.h"
#include "bk_clock.h"
#include "bk_motion.h"
#include "bk_phrase.h"
#include "bk_metadata.h"
#include <ctype.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "instance pool needs lock-free atomics");

/* Four chain slots can each hold one; spares cover a swap in flight. */
#define BK_INSTANCES 6

/* The shim parks a slot whose output stays below 5 LSB for ~1 s and renders
 * it only every ~0.5 s after that (schwung_shim.c, DSP_IDLE_THRESHOLD), so the
 * first hits after Play would be late. A block that would be silent carries a
 * 6 LSB DC offset (-75 dBFS, inaudible, removed by any DC blocker) -- except at
 * VOLUME 0, which is exact digital silence. Same scheme as FMDRUM. */
#define BK_KEEPALIVE_LSB 6
#define BK_SILENCE_LSB 4

typedef struct {
    atomic_int used;
    bk_engine engine;
    bk_clock clock;
    bk_motion motion;
    double last_pos16;       /* monotonic playhead (host beat jitter) */
    uint16_t gates;          /* trigger pads: bit n = step n+1 of 16 fires */
    bk_phrase phrase;        /* the RANDOM pad */
    uint32_t frames;         /* since create: entropy for the RANDOM pad */
    /* MIDI clock fallback, used only when the host has no get_beat_position. */
    int clk_running, clk_awaiting;
    long clk_ticks;
    /* A 1/16 that falls in the last half LXR block of a Move block lands on
     * the next block's first edge: carried here. */
    int pending;
    int64_t pending_abs16;
} bk_instance;

static bk_instance g_pool[BK_INSTANCES];
static const host_api_v1_t *g_host;
static atomic_uint g_seed = 0x5B1CA7E1u;

/* The knob grid is Schwung's own page engine, driven from ui_chain.js so the
 * module sees the knobs' touch sensors. Served as `grid_hierarchy`, while
 * `ui_hierarchy` is served EMPTY on purpose: a hierarchy there makes the
 * shadow UI draw its stock grid, which runs no module code and so never sees
 * a knob touch (FMDRUM's arrangement). */
#define FXP(key, type) "{\"key\":\"" key "\",\"visible_if\":{\"param\":\"fx_type\",\"equals\":" #type "}}"
static const char bk_hierarchy[] =
    "{\"modes\":null,\"levels\":{"
      "\"root\":{\"label\":\"LUXOR\","
        "\"knobs\":[\"volume\",\"p_env\",\"fm_amount\",\"mod_decay\",\"cutoff\",\"drive\",\"decay\",\"pitch\"],"
        "\"params\":[\"volume\",\"p_env\",\"fm_amount\",\"mod_decay\",\"cutoff\",\"drive\",\"decay\",\"pitch\","
          "{\"level\":\"osc\",\"label\":\"Osc\"},{\"level\":\"fm\",\"label\":\"FM\"},"
          "{\"level\":\"filter\",\"label\":\"Filter\"},{\"level\":\"fx\",\"label\":\"FX\"},"
          "{\"level\":\"lfo\",\"label\":\"LFO\"},{\"level\":\"scale\",\"label\":\"Scale\"}]},"
      "\"osc\":{\"label\":\"Osc\","
        "\"knobs\":[\"wave\",\"pwm\",\"attack\",\"a_slope\",\"p_slope\"],"
        "\"params\":[\"wave\",\"pwm\",\"attack\",\"a_slope\",\"p_slope\"]},"
      "\"fm\":{\"label\":\"FM\","
        "\"knobs\":[\"fm_freq\",\"mod_wave\",\"fm_mode\",\"trans\",\"tr_vol\",\"tr_freq\"],"
        "\"params\":[\"fm_freq\",\"mod_wave\",\"fm_mode\",\"trans\",\"tr_vol\",\"tr_freq\"]},"
      "\"filter\":{\"label\":\"Filter\","
        "\"knobs\":[\"reso\",\"f_type\",\"f_drive\",\"pan\",\"s_rate\"],"
        "\"params\":[\"reso\",\"f_type\",\"f_drive\",\"pan\",\"s_rate\"]},"
      "\"lfo\":{\"label\":\"LFO\","
        "\"knobs\":[\"lfo_rate\",\"lfo_sync\",\"lfo_amt\",\"lfo_wave\",\"lfo_retrig\",\"lfo_phase\",\"lfo_dest\"],"
        "\"params\":[\"lfo_rate\",\"lfo_sync\",\"lfo_amt\",\"lfo_wave\",\"lfo_retrig\",\"lfo_phase\",\"lfo_dest\"]},"
      /* Knob 1 MIX, knob 2 TYPE, then the selected FX's own parameters: the
       * others are hidden (visible_if on fx_type, evaluated by ui_core.mjs),
       * so knobs 3.. are always the FX in use and nothing else. */
      "\"fx\":{\"label\":\"FX\","
        "\"knobs\":[\"fx_mix\",\"fx_type\",\"drv_type\",\"drv_drive\",\"drv_color\",\"drv_tone\",\"drv_vol\","
          "\"ring_wave\",\"ring_freq\",\"cmp_ratio\",\"cmp_attack\",\"cmp_decay\",\"cmp_thresh\",\"cmp_gain\","
          "\"dly_time\",\"dly_range\",\"dly_tone\",\"dly_fdbk\",\"pp_time\",\"pp_range\",\"pp_tone\",\"pp_fdbk\"],"
        "\"params\":[\"fx_mix\",\"fx_type\","
          FXP("drv_type",1) "," FXP("drv_drive",1) "," FXP("drv_color",1) "," FXP("drv_tone",1) "," FXP("drv_vol",1) ","
          FXP("ring_wave",2) "," FXP("ring_freq",2) ","
          FXP("cmp_ratio",3) "," FXP("cmp_attack",3) "," FXP("cmp_decay",3) "," FXP("cmp_thresh",3) "," FXP("cmp_gain",3) ","
          FXP("dly_time",4) "," FXP("dly_range",4) "," FXP("dly_tone",4) "," FXP("dly_fdbk",4) ","
          FXP("pp_time",5) "," FXP("pp_range",5) "," FXP("pp_tone",5) "," FXP("pp_fdbk",5) "]},"
      /* The last page: what is set once and left -- the scale PITCH steps
       * through, the loop length, and the LXR's TUNE / FINE. */
      "\"scale\":{\"label\":\"Scale\","
        "\"knobs\":[\"root\",\"scale\",\"length\",\"tune\",\"fine\"],"
        "\"params\":[\"root\",\"scale\",\"length\",\"tune\",\"fine\"]}"
    "}}";
#undef FXP

/* ---- lifecycle ---------------------------------------------------------- */
static void *create_instance(const char *module_dir, const char *defaults) {
    (void)module_dir; (void)defaults;
    for (int i = 0; i < BK_INSTANCES; ++i) {
        int expected = 0;
        if (atomic_compare_exchange_strong(&g_pool[i].used, &expected, 1)) {
            bk_instance *inst = &g_pool[i];
            bk_engine_init(&inst->engine, atomic_fetch_add(&g_seed, 0x9E3779B9u));
            bk_clock_init(&inst->clock);
            bk_motion_init(&inst->motion);
            inst->last_pos16 = -1.0;
            inst->gates = 0xFFFFu;
            bk_phrase_init(&inst->phrase, atomic_fetch_add(&g_seed, 0x7F4A7C15u));
            inst->frames = 0;
            inst->clk_running = inst->clk_awaiting = 0;
            inst->clk_ticks = 0;
            inst->pending = 0;
            inst->pending_abs16 = 0;
            return inst;
        }
    }
    return NULL;
}

static void destroy_instance(void *ptr) {
    if (ptr) atomic_store(&((bk_instance *)ptr)->used, 0);
}

/* ---- MIDI: only the clock (fallback transport) -------------------------- */
static void on_midi(void *ptr, const uint8_t *msg, int len, int source) {
    (void)source;
    if (!ptr || !msg || len < 1) return;
    bk_instance *inst = ptr;
    switch (msg[0]) {
    case 0xFA: inst->clk_running = 1; inst->clk_awaiting = 1; inst->clk_ticks = 0; break;
    case 0xFB: inst->clk_running = 1; break;
    case 0xFC: inst->clk_running = 0; break;
    case 0xF8:
        if (!inst->clk_running) { inst->clk_running = 1; inst->clk_awaiting = 1; }
        if (inst->clk_awaiting) { inst->clk_awaiting = 0; inst->clk_ticks = 0; }
        else inst->clk_ticks++;
        break;
    default: break;          /* notes, CCs: this voice plays itself */
    }
}

/* ---- parameters --------------------------------------------------------- */
static int param_index(const char *key) {
    for (int i = 0; i < BP_COUNT; ++i) if (!strcmp(key, bk_params[i].key)) return i;
    return -1;
}

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) ++p;
    return p;
}

/* A whole-string number, rejecting NaN/Inf/trailing junk. */
static int parse_float(const char *s, float *out) {
    char *end;
    float v = strtof(s, &end);
    if (end == s || *skip_ws(end) || !isfinite(v)) return 0;
    *out = v;
    return 1;
}

/* ---- state blob -----------------------------------------------------------
 * {"v":4,"p":[BP_COUNT numbers],"m":"<lane>:<data>;...","g":<gates>}
 * Self-contained, so autosave and My Presets reproduce the sound exactly:
 * every LXR voice parameter, the FX type, every FX's own settings, the mix,
 * root / scale / length, the trigger pads, the motion lanes (the RANDOM
 * melody lives in the PITCH lane). The REC pad is never saved.
 * "m" carries only lanes that hold automation; <data> is BK_POINTS 12-bit
 * values normalized over the parameter's range, two base64url characters
 * each (4095 = point not written). Applied only if every field is valid.
 *
 * v1-v3 are the pre-LXR LUXOR, a different instrument: its sound knobs
 * meant other things and are NOT carried over. What still means the same is:
 * VOLUME, PITCH (and the PITCH lane: the RANDOM melody), ROOT, SCALE,
 * LENGTH and the trigger pads (v3). Everything else starts from the defaults.
 * v1/v2 PITCH (a 0..1 knob / a semitone position) is converted to the index
 * of the note it played, as before. */
#define BK_STATE_MAX 16000

static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

static const char *find_key(const char *s, const char *key) {
    size_t n = strlen(key);
    for (const char *p = s; (p = strchr(p, '"')) != NULL; ++p) {
        if (!strncmp(p + 1, key, n) && p[n + 1] == '"') {
            const char *q = skip_ws(p + n + 2);
            if (*q == ':') return skip_ws(q + 1);
        }
    }
    return NULL;
}

/* JSON number grammar only: strtof alone would also take "nan", hex, "+1". */
static const char *read_number(const char *p, float *out) {
    const char *start = p;
    if (*p == '-') ++p;
    if (*p == '0') ++p;
    else if (*p >= '1' && *p <= '9') { while (isdigit((unsigned char)*p)) ++p; }
    else return NULL;
    if (*p == '.') {
        ++p;
        if (!isdigit((unsigned char)*p)) return NULL;
        while (isdigit((unsigned char)*p)) ++p;
    }
    if (*p == 'e' || *p == 'E') {
        ++p;
        if (*p == '+' || *p == '-') ++p;
        if (!isdigit((unsigned char)*p)) return NULL;
        while (isdigit((unsigned char)*p)) ++p;
    }
    char tmp[32];
    size_t n = (size_t)(p - start);
    if (n == 0 || n >= sizeof(tmp)) return NULL;
    memcpy(tmp, start, n);
    tmp[n] = 0;
    float v = strtof(tmp, NULL);
    if (!isfinite(v)) return NULL;
    *out = v;
    return p;
}

/* "p":[count numbers] */
static int read_array(const char *s, float *out, int count) {
    const char *p = find_key(s, "p");
    if (!p || *p != '[') return 0;
    p = skip_ws(p + 1);
    for (int i = 0; i < count; ++i) {
        p = read_number(p, &out[i]);
        if (!p) return 0;
        p = skip_ws(p);
        if (i < count - 1) {
            if (*p != ',') return 0;
            p = skip_ws(p + 1);
        }
    }
    return *p == ']';
}

/* The pre-LXR lane names, in their old lane order. */
static const char *const legacy_lane_keys[] = {
    "vcf_decay", "vca_decay", "fm_amount", "vcf_env", "cutoff", "reso_drive", "pitch",
    "p_env", "p_decay",
};
#define LEGACY_LANES 9

/* "m":"<lane>:<data>;..." into scratch rows. `lane_of` maps a name to a
 * scratch row (-1 = unknown name: the state is refused); a row of -2 is
 * parsed and dropped. */
static int read_lanes(const char *s, uint16_t (*rows)[BK_POINTS], int nrows, int (*lane_of)(const char *)) {
    for (int k = 0; k < nrows; ++k)
        for (int i = 0; i < BK_POINTS; ++i) rows[k][i] = BK_EMPTY;
    const char *p = find_key(s, "m");
    if (!p || *p != '"') return 0;
    ++p;
    unsigned seen = 0;
    while (*p != '"') {
        const char *colon = strchr(p, ':');
        if (!colon || colon - p >= 24) return 0;
        char key[24];
        memcpy(key, p, (size_t)(colon - p));
        key[colon - p] = 0;
        int k = lane_of(key);
        if (k == -1 || (k >= 0 && (seen & (1u << k)))) return 0;
        if (k >= 0) seen |= 1u << k;
        p = colon + 1;
        for (int i = 0; i < BK_POINTS; ++i) {
            int hi = b64_val(p[0]), lo = hi < 0 ? -1 : b64_val(p[1]);
            if (hi < 0 || lo < 0) return 0;
            int q = hi * 64 + lo;
            if (k >= 0) rows[k][i] = q == 4095 ? BK_EMPTY : (q > BK_QMAX ? BK_QMAX : (uint16_t)q);
            p += 2;
        }
        if (*p == ';') ++p;
        else if (*p != '"') return 0;
    }
    return 1;
}

static int lane_of_current(const char *key) { return bk_motion_lane(param_index(key)); }
/* Old lanes: only PITCH still means the same thing. */
static int lane_of_legacy(const char *key) {
    for (int i = 0; i < LEGACY_LANES; ++i)
        if (!strcmp(key, legacy_lane_keys[i])) return i == 6 ? 0 : -2;
    return -1;
}

/* "g":<gates>, which closes the document (a cut-off document is refused). */
static int read_gates(const char *s, float *g) {
    const char *p = find_key(s, "g");
    if (!p || !(p = read_number(p, g)) || *g != floorf(*g) || *g < 0.0f || *g > 65535.0f) return 0;
    return *skip_ws(p) == '}';
}

/* Parse into scratch first; the instance changes only if all of it is valid.
 * Static scratch is safe: every call comes from the one SPI callback thread. */
static uint16_t g_scratch[BK_MOTION_LANES][BK_POINTS];
static uint16_t g_legacy_pitch[1][BK_POINTS];

/* A v1/v2 PITCH (semitones above C1, 0..48) as a scale index. */
static float legacy_pitch_index(float semis, int root, int scale) {
    return (float)bk_pitch_index_of_midi((float)bk_quantize_pitch(semis, root, scale), root, scale);
}

static int valid_value(int i, float v) {
    const bk_param_info *pi = &bk_params[i];
    if (v < pi->min || v > pi->max) return 0;
    return !pi->is_int || v == floorf(v);
}

static void apply_all(bk_instance *inst, const float *prm, unsigned gates) {
    bk_engine *e = &inst->engine;
    /* Root and scale first: PITCH is clamped to the scale in force. */
    bk_engine_set_param(e, BP_ROOT, prm[BP_ROOT]);
    bk_engine_set_param(e, BP_SCALE, prm[BP_SCALE]);
    for (int i = 0; i < BP_COUNT; ++i) bk_engine_set_param(e, i, prm[i]);
    bk_motion_clear(&inst->motion, e, -1);
    memcpy(inst->motion.pt, g_scratch, sizeof(inst->motion.pt));
    inst->gates = (uint16_t)gates;
}

static int restore_v4(bk_instance *inst, const char *s) {
    float prm[BP_COUNT], g;
    if (!read_array(s, prm, BP_COUNT)) return 0;
    for (int i = 0; i < BP_COUNT; ++i) if (!valid_value(i, prm[i])) return 0;
    if (prm[BP_PITCH] > (float)(bk_pitch_count((int)prm[BP_ROOT], (int)prm[BP_SCALE]) - 1)) return 0;
    if (!read_lanes(s, g_scratch, BK_MOTION_LANES, lane_of_current)) return 0;
    if (!read_gates(s, &g)) return 0;
    apply_all(inst, prm, (unsigned)g);
    return 1;
}

/* Old layout: volume,vcf_decay,vca_decay,fm_amount,vcf_env,cutoff,reso_drive,
 * pitch,root,scale,p_env,p_decay,vco2_tune,vco2_level,noise,length[,rec]. */
enum { OLD_VOLUME = 0, OLD_PITCH = 7, OLD_ROOT = 8, OLD_SCALE = 9, OLD_LENGTH = 15, OLD_COUNT = 16 };

static int restore_legacy(bk_instance *inst, const char *s, float v) {
    float raw[OLD_COUNT + 1], prm[BP_COUNT], g = 65535.0f;
    const int count = v >= 3.0f ? OLD_COUNT : OLD_COUNT + 1;
    if (!read_array(s, raw, count)) return 0;
    const float vol = raw[OLD_VOLUME], root = raw[OLD_ROOT], scale = raw[OLD_SCALE], len = raw[OLD_LENGTH];
    float pitch = raw[OLD_PITCH];
    if (!(vol >= 0.0f && vol <= 1.0f)) return 0;
    if (root != floorf(root) || root < 0.0f || root > 11.0f) return 0;
    if (scale != floorf(scale) || scale < 0.0f || scale > 11.0f) return 0;
    if (len != floorf(len) || len < 0.0f || len > 3.0f) return 0;
    const int r = (int)root, sc = (int)scale;
    if (v == 1.0f) {
        if (!(pitch >= 0.0f && pitch <= 1.0f)) return 0;
        pitch = floorf(pitch * 48.0f + 0.5f);
    }
    if (v < 3.0f) {
        if (!(pitch >= 0.0f && pitch <= 48.0f)) return 0;
        pitch = legacy_pitch_index(pitch, r, sc);
    } else if (pitch != floorf(pitch) || pitch < 0.0f || pitch > (float)(bk_pitch_count(r, sc) - 1)) {
        return 0;
    }
    if (!read_lanes(s, g_legacy_pitch, 1, lane_of_legacy)) return 0;
    if (v >= 3.0f && !read_gates(s, &g)) return 0;

    for (int i = 0; i < BP_COUNT; ++i) prm[i] = bk_params[i].def;
    prm[BP_VOLUME] = vol;
    prm[BP_PITCH] = pitch;
    prm[BP_ROOT] = root;
    prm[BP_SCALE] = scale;
    prm[BP_LENGTH] = len;
    for (int k = 0; k < BK_MOTION_LANES; ++k)
        for (int i = 0; i < BK_POINTS; ++i) g_scratch[k][i] = BK_EMPTY;
    /* The PITCH lane: normalized over the same 0..48 index range as now;
     * v1/v2 lanes held semitone positions, which become the notes they played. */
    const int pk = bk_motion_lane(BP_PITCH);
    for (int i = 0; i < BK_POINTS; ++i) {
        uint16_t q = g_legacy_pitch[0][i];
        if (q != BK_EMPTY && v < 3.0f) {
            float semis = bk_motion_dq(q) * 48.0f;
            q = bk_motion_q(bk_motion_norm(BP_PITCH, legacy_pitch_index(floorf(semis + 0.5f), r, sc)));
        }
        g_scratch[pk][i] = q;
    }
    apply_all(inst, prm, (unsigned)g);
    return 1;
}

static int restore_state(bk_instance *inst, const char *text) {
    if (!text || strlen(text) > BK_STATE_MAX) return 0;
    const char *s = skip_ws(text);
    if (*s != '{') return 0;
    float v;
    const char *p = find_key(s, "v");
    if (!p || !read_number(p, &v)) return 0;
    if (v == 4.0f) return restore_v4(inst, s);
    if (v == 1.0f || v == 2.0f || v == 3.0f) return restore_legacy(inst, s, v);
    return 0;
}

static int write_state(const bk_engine *e, const bk_motion *mo, unsigned gates, char *buf, int size) {
    int n = snprintf(buf, size, "{\"v\":4,\"p\":[");
    for (int i = 0; i < BP_COUNT && n > 0 && n < size; ++i)
        n += bk_params[i].is_int ? snprintf(buf + n, size - n, "%s%d", i ? "," : "", (int)e->param[i])
                                 : snprintf(buf + n, size - n, "%s%.4f", i ? "," : "", e->param[i]);
    if (n > 0 && n < size) n += snprintf(buf + n, size - n, "],\"m\":\"");
    unsigned mask = bk_motion_mask(mo);
    int first = 1;
    for (int k = 0; k < BK_MOTION_LANES && n > 0 && n < size; ++k) {
        if (!(mask & (1u << k))) continue;
        n += snprintf(buf + n, size - n, "%s%s:", first ? "" : ";", bk_params[bk_motion_param[k]].key);
        first = 0;
        if (n < 0 || n + 2 * BK_POINTS >= size) { buf[0] = 0; return -1; }
        for (int i = 0; i < BK_POINTS; ++i) {
            int q = mo->pt[k][i] == BK_EMPTY ? 4095 : mo->pt[k][i];
            buf[n++] = b64[q >> 6];
            buf[n++] = b64[q & 63];
        }
        buf[n] = 0;
    }
    if (n > 0 && n < size) n += snprintf(buf + n, size - n, "\",\"g\":%u}", gates);
    if (n < 0 || n >= size) { buf[0] = 0; return -1; }
    return n;
}

/* "touch" = "<key>,<0|1>" from ui_chain.js: the knob's capacitive sensor.
 * "motion_clear" = "<key>" or "all". */
static void set_touch(bk_instance *inst, const char *val) {
    char key[32];
    const char *comma = strchr(val, ',');
    if (!comma || (size_t)(comma - val) >= sizeof(key)) return;
    memcpy(key, val, (size_t)(comma - val));
    key[comma - val] = 0;
    int lane = bk_motion_lane(param_index(key));
    if (lane < 0 || (strcmp(comma + 1, "0") && strcmp(comma + 1, "1"))) return;
    bk_motion_touch(&inst->motion, &inst->engine, lane, comma[1] == '1');
}

/* A new root or scale re-snaps PITCH -- the knob, what is playing, and every
 * recorded pitch point -- to the nearest note of the new scale, so the voice
 * does not jump to whatever note now sits at the old index. */
static void set_key(bk_instance *inst, int idx, float f) {
    bk_engine *e = &inst->engine;
    int r0 = (int)e->param[BP_ROOT], s0 = (int)e->param[BP_SCALE];
    float old_pitch = e->param[BP_PITCH];
    bk_engine_set_param(e, idx, f);
    int r1 = (int)e->param[BP_ROOT], s1 = (int)e->param[BP_SCALE];
    if (r0 == r1 && s0 == s1) return;
#define RESNAP(i) (float)bk_pitch_index_of_midi((float)bk_pitch_midi((int)((i) + 0.5f), r0, s0), r1, s1)
    bk_engine_set_param(e, BP_PITCH, RESNAP(old_pitch));
    if (!isnan(e->ovr[BP_PITCH])) e->ovr[BP_PITCH] = RESNAP(e->ovr[BP_PITCH]);
    int pk = bk_motion_lane(BP_PITCH);
    for (int i = 0; i < BK_POINTS; ++i) {
        uint16_t q = inst->motion.pt[pk][i];
        if (q == BK_EMPTY) continue;
        float x = bk_motion_denorm(BP_PITCH, bk_motion_dq(q));
        inst->motion.pt[pk][i] = bk_motion_q(bk_motion_norm(BP_PITCH, RESNAP(x)));
    }
#undef RESNAP
}

/* The RANDOM pad: a new Phrase Gen phrase, written as PITCH automation --
 * each note set as a knob value of the scale in use (nearest note,
 * transposed to ROOT), never quantized afterwards. Phrase Gen's sources span
 * ~6 octaves, far too wide for a percussion voice, so each note is folded by
 * octaves into a two-octave window around the knob's own note (a fifth below
 * to 17 semitones above): the melody keeps its note names, knob 8 sets its
 * register. The sources sit high, so the fold WRAPS over two octaves first:
 * only folding them down piled every note into the window's top octave. At the
 * ends of the range the window is cut, never slid, so a C1 knob stays at C1
 * to F2. Every point of a step holds its note (a hit reads the step's
 * first); a 1/2-bar loop gets an 8-step phrase, anything longer a 16-step
 * one, repeated to the loop's end. Same as LXRDRUM's RANDOM pad. */
static void randomize(bk_instance *inst) {
    bk_engine *e = &inst->engine;
    int len = bk_motion_len_steps(e), plen = len == 8 ? 8 : 16, notes[16];
    if (!bk_phrase_generate(&inst->phrase, plen, inst->frames, notes)) return;
    int r = (int)e->param[BP_ROOT], sc = (int)e->param[BP_SCALE];
    int pk = bk_motion_lane(BP_PITCH);
    int knob = bk_pitch_midi((int)e->param[BP_PITCH], r, sc);
    int lo = knob - 7, hi = knob + 17;
    if (lo < BK_PITCH_LO) lo = BK_PITCH_LO;
    if (hi > BK_PITCH_HI) hi = BK_PITCH_HI;
    for (int st = 0; st < len; ++st) {
        int n = notes[st % plen];
        if (n < 0) n = knob - r;
        n += r;                                   /* the phrases are in C */
        n = lo + (((n - lo) % 24) + 24) % 24;
        while (n > hi) n -= 12;
        while (n < lo) n += 12;
        while (n > BK_PITCH_HI) n -= 12;
        while (n < BK_PITCH_LO) n += 12;
        uint16_t q = bk_motion_q(bk_motion_norm(BP_PITCH, (float)bk_pitch_index_of_midi((float)n, r, sc)));
        for (int j = 0; j < BK_SUB; ++j) inst->motion.pt[pk][st * BK_SUB + j] = q;
    }
}

static void set_param(void *ptr, const char *key, const char *val) {
    if (!ptr || !key || !val) return;
    bk_instance *inst = ptr;
    bk_engine *e = &inst->engine;
    float f;
    if (!strcmp(key, "state")) { (void)restore_state(inst, val); return; }
    /* The pads. */
    if (!strcmp(key, "write")) {
        if (!strcmp(val, "0") || !strcmp(val, "1")) inst->motion.write = val[0] == '1';
        return;
    }
    if (!strcmp(key, "gate_toggle")) {
        if (parse_float(val, &f) && f == floorf(f) && f >= 0.0f && f < 16.0f)
            inst->gates ^= (uint16_t)(1u << (int)f);
        return;
    }
    if (!strcmp(key, "randomize")) { randomize(inst); return; }
    if (!strcmp(key, "touch")) { set_touch(inst, val); return; }
    if (!strcmp(key, "motion_clear")) {
        if (!strcmp(val, "all")) bk_motion_clear(&inst->motion, e, -1);
        else {
            int k = bk_motion_lane(param_index(val));
            if (k >= 0) bk_motion_clear(&inst->motion, e, k);
        }
        return;
    }
    int idx = param_index(key);
    if (idx < 0 || !parse_float(val, &f)) return;
    if (idx == BP_ROOT || idx == BP_SCALE) { set_key(inst, idx, f); return; }
    bk_engine_set_param(e, idx, f);
}

static int copy_text(char *buf, int size, const char *text) {
    size_t n = strlen(text);
    if (n >= (size_t)size) { buf[0] = 0; return -1; }
    memcpy(buf, text, n + 1);
    return (int)n;
}

static int format_value(const bk_param_info *pi, float x, char *buf, int size) {
    return pi->is_int ? snprintf(buf, size, "%d", (int)x) : snprintf(buf, size, "%.4f", x);
}

static int get_param(void *ptr, const char *key, char *buf, int size) {
    if (!ptr || !key || !buf || size <= 0) return -1;
    buf[0] = 0;
    const bk_instance *inst = ptr;
    const bk_engine *e = &inst->engine;
    int idx = param_index(key);
    int n = -1;
    if (idx >= 0) n = format_value(&bk_params[idx], e->param[idx], buf, size);
    /* eff_<key>: what the sound is using right now (automation playback). */
    else if (!strncmp(key, "eff_", 4) && (idx = param_index(key + 4)) >= 0)
        n = format_value(&bk_params[idx], bk_engine_value(e, idx), buf, size);
    /* One read for the UI:
     * "<lanes with data>,<step or -1>,<recording lanes>,<loop steps>,<REC pad>" */
    else if (!strcmp(key, "motion_status"))
        n = snprintf(buf, size, "%u,%d,%u,%d,%d", bk_motion_mask(&inst->motion),
                     inst->motion.step, bk_motion_recording(&inst->motion, e),
                     bk_motion_len_steps(e), inst->motion.write);
    /* The trigger pads, step 1 first: "1111111111111111". */
    else if (!strcmp(key, "gate_status")) {
        if (size <= 16) return -1;
        for (int i = 0; i < 16; ++i) buf[i] = (inst->gates >> i) & 1u ? '1' : '0';
        buf[16] = 0;
        return 16;
    }
    else if (!strcmp(key, "write")) n = snprintf(buf, size, "%d", inst->motion.write);
    /* The PITCH knob's note names for the scale in use, comma separated: the
     * UI shows "Eb2" rather than an index, and asks again after a key change. */
    else if (!strcmp(key, "pitch_names")) {
        int r = (int)e->param[BP_ROOT], sc = (int)e->param[BP_SCALE];
        int cnt = bk_pitch_count(r, sc);
        n = 0;
        for (int i = 0; i < cnt && n >= 0 && n < size; ++i) {
            char nm[8];
            bk_note_name(bk_pitch_midi(i, r, sc), nm, sizeof(nm));
            n += snprintf(buf + n, size - n, "%s%s", i ? "," : "", nm);
        }
    }
    /* The note the PITCH knob is set to. */
    else if (!strcmp(key, "pitch_note")) {
        bk_note_name(bk_pitch_midi((int)e->param[BP_PITCH], (int)e->param[BP_ROOT],
                                   (int)e->param[BP_SCALE]), buf, size);
        n = (int)strlen(buf);
    }
    else if (!strcmp(key, "state")) return write_state(e, &inst->motion, inst->gates, buf, size);
    /* PITCH's range is the number of notes the scale has in C1..C5, so the
     * knob has no dead travel whatever the scale (module.json carries 28,
     * C Phrygian's top index). */
    else if (!strcmp(key, "chain_params"))
        n = snprintf(buf, size, "%s%d%s", bk_chain_params_head,
                     bk_pitch_count((int)e->param[BP_ROOT], (int)e->param[BP_SCALE]) - 1,
                     bk_chain_params_tail);
    else if (!strcmp(key, "ui_hierarchy")) return 0;
    else if (!strcmp(key, "grid_hierarchy")) return copy_text(buf, size, bk_hierarchy);
    if (n < 0 || n >= size) { buf[0] = 0; return -1; }
    return n;
}

static int get_error(void *ptr, char *buf, int size) {
    (void)ptr;
    if (buf && size > 0) buf[0] = 0;
    return 0;
}

/* ---- audio -------------------------------------------------------------- */
static double transport_beat(bk_instance *inst) {
    if (g_host && g_host->get_beat_position) return g_host->get_beat_position();
    return inst->clk_running ? (double)inst->clk_ticks / 24.0 : -1.0;
}

static float transport_bpm(void) {
    float bpm = (g_host && g_host->get_bpm) ? g_host->get_bpm() : 120.0f;
    return (bpm >= 20.0f && bpm <= 999.0f) ? bpm : 120.0f;
}

/* The playhead never steps backwards by less than a 1/16 (that is host
 * jitter, not a new Play). */
static void motion_at(bk_instance *inst, double pos16) {
    if (pos16 >= 0.0 && inst->last_pos16 >= 0.0 &&
        pos16 < inst->last_pos16 && pos16 > inst->last_pos16 - 1.0)
        pos16 = inst->last_pos16;
    inst->last_pos16 = pos16;
    bk_motion_update(&inst->motion, &inst->engine, pos16);
}

/* A 1/16 boundary: automation is stepped to the hit's exact 1/16 first, so
 * PITCH (and the decays) are the values at the start of that step. The
 * trigger pads: step n of the loop fires if pad (n mod 16) is on; motion
 * still steps on a silent step, so its automation stays in time. */
static void hit(bk_instance *inst, int64_t abs16) {
    bk_engine *e = &inst->engine;
    if (inst->last_pos16 > (double)abs16) inst->last_pos16 = -1.0;   /* restart */
    motion_at(inst, (double)abs16);
    int len = bk_motion_len_steps(e);
    int step = (int)(((abs16 % len) + len) % len) % 16;
    if ((inst->gates >> step) & 1u) bk_engine_trigger(e);
}

/* The LXR renders in 64-frame blocks and reads its parameters once per block,
 * so a Move block is two LXR blocks and a hit lands on the nearest LXR block
 * edge (at most 0.73 ms early or late) -- an LXR's own triggers wait for the
 * next DMA block the same way. Automation is updated at every block edge. */
static void render_block(void *ptr, int16_t *out, int frames) {
    if (!out || frames <= 0) return;
    if (!ptr || frames != MOVE_FRAMES_PER_BLOCK) {
        memset(out, 0, (size_t)frames * 2 * sizeof(int16_t));
        return;
    }
    _Static_assert(MOVE_FRAMES_PER_BLOCK == 2 * BK_BLOCK, "a Move block is two LXR blocks");
    bk_instance *inst = ptr;
    bk_engine *e = &inst->engine;
    double beat = transport_beat(inst);
    float bpm = transport_bpm();
    double spb = (double)BK_SR * 60.0 / (double)bpm;
    bk_engine_set_bpm(e, bpm);

    inst->frames += (uint32_t)frames;
    bk_motion_age(&inst->motion, frames);
    int was_running = inst->clock.running;
    int offsets[BK_CLOCK_MAX_EVENTS];
    int n = bk_clock_advance(&inst->clock, beat, bpm, frames, offsets);
    if (!inst->clock.running) {
        if (was_running) bk_engine_release(e);
        inst->last_pos16 = -1.0;
        inst->pending = 0;
        motion_at(inst, -1.0);
    } else if (!was_running) {
        inst->last_pos16 = -1.0;             /* a new Play */
        inst->pending = 0;
    }

    int k = 0;
    for (int b = 0; b < 2; ++b) {
        const int edge = b * BK_BLOCK;
        if (inst->clock.running) motion_at(inst, (beat + (double)edge / spb) * 4.0);
        if (b == 0 && inst->pending) { inst->pending = 0; hit(inst, inst->pending_abs16); }
        /* this edge takes the hits nearest to it: offsets in [edge-32, edge+32) */
        for (; k < n && offsets[k] < edge + BK_BLOCK / 2; ++k)
            hit(inst, inst->clock.last_abs - (int64_t)(n - 1 - k));
        bk_engine_render(e, out + edge * 2);
    }
    /* the rest are nearest to the next block's first edge */
    for (; k < n; ++k) {
        inst->pending = 1;
        inst->pending_abs16 = inst->clock.last_abs - (int64_t)(n - 1 - k);
    }

    int peak = 0;
    for (int i = 0; i < frames * 2; ++i) {
        int a = abs(out[i]);
        if (a > peak) peak = a;
    }
    if (peak <= BK_SILENCE_LSB && e->param[BP_VOLUME] > 0.0f)
        for (int i = 0; i < frames * 2; ++i) out[i] = BK_KEEPALIVE_LSB;
}

static plugin_api_v2_t g_api = {
    .api_version = 2,
    .create_instance = create_instance,
    .destroy_instance = destroy_instance,
    .on_midi = on_midi,
    .set_param = set_param,
    .get_param = get_param,
    .get_error = get_error,
    .render_block = render_block,
};

/* The only exported symbol (-fvisibility=hidden): the Phrase Gen copy's
 * globals can never collide with another module's. */
__attribute__((visibility("default")))
plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
    if (host && host->sample_rate && host->sample_rate != MOVE_SAMPLE_RATE) return NULL;
    g_host = host;
    bk_phrase_library_init();
    return &g_api;
}
