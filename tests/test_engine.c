/* LUXOR engine / clock / motion tests. Built with ASan+UBSan by
 * scripts/test.sh.
 *   test-engine                 run the tests
 *   test-engine --wav <file>    also render a demo 1/16 roll (stereo 16-bit) */
#include "bk_engine.h"
#include "bk_clock.h"
#include "bk_motion.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static bk_engine E, E2;
static bk_motion M;
#define BUF_FRAMES (44100 * 70)
static int16_t buf[BUF_FRAMES * 2];

static int in_phrygian_c(int midi) {
    static const int deg[7] = {0, 1, 3, 5, 7, 8, 10};
    for (int i = 0; i < 7; ++i) if (midi % 12 == deg[i]) return 1;
    return 0;
}

static double rms(const int16_t *x, int frames) {
    double s = 0;
    for (int i = 0; i < frames * 2; ++i) s += (double)x[i] * x[i];
    return sqrt(s / (frames > 0 ? frames * 2 : 1));
}

static int peak(const int16_t *x, int frames) {
    int p = 0;
    for (int i = 0; i < frames * 2; ++i) if (abs(x[i]) > p) p = abs(x[i]);
    return p;
}

/* Render `frames` (a multiple of 64) of one engine. */
static void render(bk_engine *e, int16_t *out, int frames) {
    for (int i = 0; i < frames; i += BK_BLOCK) bk_engine_render(e, out + i * 2);
}

/* `seconds` of a hit every 1/16 at `bpm`, each hit on the nearest 64-frame
 * edge (what the plugin does). Returns frames. */
static int roll(bk_engine *e, int16_t *out, float seconds, float bpm) {
    int blocks = (int)(seconds * BK_SR) / BK_BLOCK;
    if (blocks * BK_BLOCK > BUF_FRAMES) blocks = BUF_FRAMES / BK_BLOCK;
    double step = BK_SR * 60.0 / bpm / 4.0, next = 0.0;
    for (int b = 0; b < blocks; ++b) {
        if ((double)b * BK_BLOCK + BK_BLOCK / 2 > next) { bk_engine_trigger(e); next += step; }
        bk_engine_render(e, out + b * BK_BLOCK * 2);
    }
    return blocks * BK_BLOCK;
}

static void test_scale(void) {
    int prev = 0, seen[128] = {0};
    for (int i = 0; i <= 1000; ++i) {
        int n = bk_quantize_pitch(48.0f * i / 1000.0f, 0, 0);
        CHECK(n >= BK_PITCH_LO && n <= BK_PITCH_HI, "range %d", n);
        CHECK(in_phrygian_c(n), "C Phrygian only: %d", n);
        CHECK(n >= prev, "monotonic");
        prev = n;
        seen[n] = 1;
    }
    CHECK(bk_quantize_pitch(0.0f, 0, 0) == 24 && bk_quantize_pitch(48.0f, 0, 0) == 72, "C1..C5");
    for (int n = 24; n <= 72; ++n) CHECK(seen[n] == in_phrygian_c(n), "degree %d reachable", n);
    /* the integer knob: every semitone position is a note of the scale or
     * snaps to a neighbour one semitone away, never further */
    for (int s = 0; s <= 48; ++s)
        CHECK(abs(bk_quantize_pitch((float)s, 0, 0) - (24 + s)) <= 1, "position %d", s);
    CHECK(bk_quantize_pitch(2.0f, 0, 0) == 25, "D -> Db (tie goes down)");
    CHECK(bk_quantize_pitch(3.0f, 0, 0) == 27, "Eb stays Eb");
    /* root + scale move the set; chromatic is plain rounding */
    for (int root = 0; root < 12; ++root)
        for (int sc = 0; sc < BK_SCALE_COUNT; ++sc)
            for (int i = 0; i <= 100; ++i) {
                int n = bk_quantize_pitch(0.48f * i, root, sc);
                CHECK(n >= BK_PITCH_LO && n <= BK_PITCH_HI, "range");
                if (sc == BK_SCALE_COUNT - 1)
                    CHECK(fabsf((float)n - (24.0f + i * 0.48f)) <= 0.5f, "chromatic %d", n);
                if (sc == 0) {
                    int rel = ((n - root) % 12 + 12) % 12;
                    CHECK(in_phrygian_c(rel), "phrygian on root %d: %d", root, n);
                }
            }
    char name[8];
    bk_note_name(24, name, sizeof(name)); CHECK(!strcmp(name, "C1"), "%s", name);
    bk_note_name(39, name, sizeof(name)); CHECK(!strcmp(name, "Eb2"), "%s", name);
    bk_note_name(72, name, sizeof(name)); CHECK(!strcmp(name, "C5"), "%s", name);
    CHECK(bk_quantize_pitch(NAN, 0, 0) == 24 && bk_quantize_pitch(900.0f, 0, 0) == 72, "junk safe");
    /* the knob IS a note of the scale: index -> note, every one in the scale */
    CHECK(bk_pitch_count(0, 0) == 29, "C Phrygian has 29 notes in C1..C5: %d", bk_pitch_count(0, 0));
    CHECK(bk_pitch_count(0, BK_SCALE_COUNT - 1) == 49, "chromatic 49");
    CHECK(bk_pitch_midi(0, 0, 0) == 24 && bk_pitch_midi(7, 0, 0) == 36 && bk_pitch_midi(28, 0, 0) == 72, "index -> note");
    CHECK(bk_pitch_midi(99, 0, 0) == 72 && bk_pitch_midi(-3, 0, 0) == 24, "index clamps");
    for (int sc = 0; sc < BK_SCALE_COUNT; ++sc)
        for (int root = 0; root < 12; ++root) {
            int cnt = bk_pitch_count(root, sc), prevm = 0;
            for (int i = 0; i < cnt; ++i) {
                int m = bk_pitch_midi(i, root, sc);
                CHECK(m > prevm, "ascending, no repeats (every detent is a new note)");
                prevm = m;
                CHECK(bk_pitch_index_of_midi((float)m, root, sc) == i, "round trip");
                if (sc == 0) CHECK(in_phrygian_c(((m - root) % 12 + 12) % 12), "in scale");
            }
        }
    int j = bk_quantize_pitch(900.0f, 99, -3);
    CHECK(j >= BK_PITCH_LO && j <= BK_PITCH_HI, "junk root/scale safe: %d", j);
}

static void test_clock(void) {
    bk_clock c;
    bk_clock_init(&c);
    int offs[BK_CLOCK_MAX_EVENTS], hits = 0;
    for (int b = 0; b < 100; ++b) CHECK(bk_clock_advance(&c, -1.0, 120, 128, offs) == 0, "stopped: silent");
    double beat = 0;
    for (int b = 0; b < (int)(2 * 44100 / 128); ++b) {
        hits += bk_clock_advance(&c, beat, 120, 128, offs);
        beat += 128.0 / (44100.0 * 0.5);
    }
    CHECK(hits == 16, "16 x 1/16 in 2 s at 120 BPM: %d", hits);
}

/* Every key is unique and the defaults sit inside their ranges. */
static void test_table(void) {
    for (int i = 0; i < BP_COUNT; ++i) {
        const bk_param_info *p = &bk_params[i];
        CHECK(p->def >= p->min && p->def <= p->max, "%s default in range", p->key);
        for (int j = 0; j < i; ++j) CHECK(strcmp(p->key, bk_params[j].key), "duplicate key %s", p->key);
    }
    CHECK(bk_params[BP_LFO_DEST].max == (float)(lxr_lfo_target_count - 1), "LFO DEST covers every target");
    CHECK(bk_params[BP_FX_TYPE].max == (float)(LXR_FX_TYPES - 1), "FX TYPE covers every FX");
}

/* Silent until the first hit; a hit sounds; it decays back to silence. */
static void test_hit(void) {
    bk_engine_init(&E, 1);
    render(&E, buf, 4096);
    CHECK(peak(buf, 4096) == 0, "silent before the first hit");
    bk_engine_trigger(&E);
    render(&E, buf, 44100 / 64 * 64);
    CHECK(peak(buf, 2048) > 3000, "the default hit is loud enough: %d", peak(buf, 2048));
    CHECK(peak(buf + 2 * 30000, 4000) < 300, "and decays: %d", peak(buf + 2 * 30000, 4000));
    CHECK(E.hits == 1, "one hit");
    /* the voice is the LXR's: the knobs reached it */
    CHECK(E.lxr.v.vol == bk_params[BP_VOLUME].def, "VOLUME is the LXR voice volume");
    CHECK((E.lxr.v.osc.midiFreq >> 8) == 60, "TUNE 0 = coarse 60: the PITCH note plays at its pitch");
}

/* PITCH: the hit is the scale note, at the note's frequency (sine, no
 * envelopes on pitch, filter open). */
static void test_pitch(void) {
    for (int idx = 0; idx <= 28; idx += 7) {
        bk_engine_init(&E, 1);
        bk_engine_set_param(&E, BP_P_ENV, 0.0f);
        bk_engine_set_param(&E, BP_FM_AMOUNT, 0.0f);
        bk_engine_set_param(&E, BP_TR_VOL, 0.0f);
        bk_engine_set_param(&E, BP_DRIVE, 0.0f);
        bk_engine_set_param(&E, BP_CUTOFF, 1.0f);
        bk_engine_set_param(&E, BP_F_TYPE, 7);       /* filter off */
        bk_engine_set_param(&E, BP_DECAY, 1.0f);
        bk_engine_set_param(&E, BP_PITCH, (float)idx);
        bk_engine_trigger(&E);
        int midi = bk_pitch_midi(idx, 0, 0);
        CHECK(E.lxr.v.osc.baseNote == midi, "the hit plays note %d", midi);
        CHECK(in_phrygian_c(midi), "a C Phrygian note");
        render(&E, buf, 44096);
        /* first crossing to last: DECAY's longest is ~0.5 s, too few cycles
         * of the lowest note for a count over a fixed window to resolve 2 %. */
        int crossings = 0, first = -1, last = -1;
        for (int i = 2 * 2205; i < 2 * 17640; i += 2)
            if ((buf[i] < 0) != (buf[i + 2] < 0)) {
                if (first < 0) first = i / 2;
                last = i / 2;
                ++crossings;
            }
        double hz = (crossings - 1) / 2.0 / ((last - first) / 44100.0);
        double want = 440.0 * pow(2.0, (midi - 69) / 12.0);
        CHECK(fabs(hz / want - 1.0) < 0.02, "note %d: %.1f Hz, want %.1f", midi, hz, want);
    }
    bk_engine_set_param(&E, BP_PITCH, 40.0f);
    CHECK(E.param[BP_PITCH] == 28.0f, "PITCH clamps to the scale's top note");
    /* TUNE transposes on top of PITCH, FINE detunes */
    bk_engine_init(&E, 1);
    bk_engine_set_param(&E, BP_TUNE, 12);
    bk_engine_set_param(&E, BP_FINE, 64);
    bk_engine_trigger(&E);
    CHECK((E.lxr.v.osc.midiFreq >> 8) == 72 && (E.lxr.v.osc.midiFreq & 0xff) == 127, "TUNE / FINE");
}

/* A roll keeps the previous hit ringing into the next (the LXR's amp EG
 * restarts its attack FROM the current level, never from zero). */
static void test_roll_continuity(void) {
    bk_engine_init(&E, 1);
    bk_engine_set_param(&E, BP_DECAY, 0.75f);
    int n = roll(&E, buf, 2.0f, 150.0f);
    int step = (int)(BK_SR * 60.0 / 150.0 / 4.0);
    int quiet = 0;
    for (int s = 2; s < 15; ++s) {
        int edge = s * step;
        double r = rms(buf + 2 * (edge - 256), 256);
        if (r < 200.0) quiet++;
    }
    CHECK(quiet == 0, "no silent gap before %d of the hits", quiet);
    CHECK(peak(buf, n) <= 32768, "bounded");
}

/* VOLUME 0 is exact digital silence; Stop fades out in ~25 ms. */
static void test_volume_and_release(void) {
    bk_engine_init(&E, 1);
    bk_engine_set_param(&E, BP_VOLUME, 0.0f);
    bk_engine_trigger(&E);
    render(&E, buf, 4096);
    CHECK(peak(buf, 4096) == 0, "VOLUME 0 is silent");
    bk_engine_init(&E, 1);
    bk_engine_set_param(&E, BP_DECAY, 1.0f);
    bk_engine_trigger(&E);
    render(&E, buf, 4416);
    CHECK(peak(buf + 2 * 4000, 400) > 1000, "sounding");
    bk_engine_release(&E);
    render(&E, buf, 1216);
    CHECK(peak(buf + 2 * 1152, 64) == 0, "silent 26 ms after Stop: %d", peak(buf + 2 * 1152, 64));
    CHECK(peak(buf, 64) > 1000, "a fade, not a cut");
    bk_engine_trigger(&E);
    render(&E, buf, 2048);
    CHECK(peak(buf, 2048) > 1000, "the next hit cancels the fade");
}

/* Every parameter at its extremes, in random combinations: always finite
 * (int16 output, but no NaN state either), and the output returns to quiet
 * once nothing plays -- the LXR's FX that sit on a DC offset when idle (Tube,
 * Fold) are removed by the output stage. */
static void test_extremes(void) {
    uint32_t r = 12345;
    for (int trial = 0; trial < 120; ++trial) {
        bk_engine_init(&E, (uint32_t)trial + 1);
        for (int p = 0; p < BP_COUNT; ++p) {
            r ^= r << 13; r ^= r >> 17; r ^= r << 5;
            const bk_param_info *pi = &bk_params[p];
            float v = (r % 3 == 0) ? pi->min : (r % 3 == 1) ? pi->max : pi->min + (pi->max - pi->min) * (float)((r >> 8) % 1000) / 1000.0f;
            if (p == BP_PITCH) v = (float)((r >> 4) % 29);
            if (p == BP_VOLUME) v = 1.0f;
            bk_engine_set_param(&E, p, v);
        }
        if (trial % 4 == 0) bk_engine_set_param(&E, BP_FX_TYPE, (float)(trial / 4 % LXR_FX_TYPES));
        int n = roll(&E, buf, 1.0f, 200.0f);
        CHECK(peak(buf, n) <= 32768, "bounded");
        for (int c = 0; c < 2; ++c) CHECK(isfinite(E.dc_y[c]), "finite output stage");
        CHECK(isfinite(E.lxr.v.filter.s1) && isfinite(E.lxr.v.filter.s2), "finite filter state");
        /* stopped: everything fades and stays silent */
        bk_engine_release(&E);
        render(&E, buf, 44096);
        CHECK(peak(buf + 2 * 4096, 40000) == 0, "trial %d: silent after Stop", trial);
    }
}

/* High delay feedback never runs away (the LXR clamps its delay line to
 * [-1,1] in half precision), with either delay, for a minute. */
static void test_delay_feedback(void) {
    for (int type = LXR_FX_DELAY; type <= LXR_FX_PINGPONG; ++type) {
        bk_engine_init(&E, 3);
        bk_engine_set_param(&E, BP_FX_TYPE, (float)type);
        bk_engine_set_param(&E, BP_FX_MIX, 1.0f);
        bk_engine_set_param(&E, type == LXR_FX_DELAY ? BP_DLY_FDBK : BP_PP_FDBK, 1.0f);
        bk_engine_set_param(&E, type == LXR_FX_DELAY ? BP_DLY_TONE : BP_PP_TONE, 1.0f);
        bk_engine_set_param(&E, BP_DRIVE, 1.0f);
        int n = roll(&E, buf, 60.0f, 300.0f);
        double first = rms(buf, 44100), last = rms(buf + 2 * (n - 44100), 44100);
        CHECK(peak(buf, n) <= 32768, "bounded");
        CHECK(last < 4.0 * first + 2000.0, "type %d: no runaway (%.0f -> %.0f rms)", type, first, last);
        CHECK(fabs((double)E.dc_y[0]) < 32768.0, "finite");
    }
}

/* Changing FX while it plays crossfades (10 ms) instead of jumping. */
static int max_step(const int16_t *x, int frames) {
    int m = 0;
    for (int i = 2; i < frames * 2; ++i) if (abs(x[i] - x[i - 2]) > m) m = abs(x[i] - x[i - 2]);
    return m;
}

static void test_fx_switch(void) {
    bk_engine_init(&E, 4);
    bk_engine_set_param(&E, BP_DECAY, 1.0f);
    bk_engine_set_param(&E, BP_WAVE, 0);
    bk_engine_set_param(&E, BP_FX_TYPE, LXR_FX_DIST);
    bk_engine_set_param(&E, BP_FX_MIX, 1.0f);
    bk_engine_set_param(&E, BP_DRV_DRIVE, 1.0f);
    bk_engine_set_param(&E, BP_DRV_VOL, 1.0f);
    bk_engine_trigger(&E);
    render(&E, buf, 8192);
    int before = max_step(buf + 2 * 4096, 4096);
    bk_engine_set_param(&E, BP_FX_TYPE, LXR_FX_OFF);
    render(&E, buf, 2048);
    int at_switch = max_step(buf, 2048);
    CHECK(at_switch <= before + 2000, "FX change is not a click: step %d vs %d while playing", at_switch, before);
    CHECK(E.lxr.fx_fade == 0, "the crossfade ends");
    /* each FX keeps its own settings */
    bk_engine_set_param(&E, BP_FX_TYPE, LXR_FX_RING);
    bk_engine_set_param(&E, BP_RING_FREQ, 0.9f);
    bk_engine_set_param(&E, BP_FX_TYPE, LXR_FX_DIST);
    render(&E, buf, 128);
    CHECK(E.lxr.fx_par[LXR_FX_RING][0] == 0.9f && E.lxr.fx_par[LXR_FX_DIST][0] == 1.0f, "per-FX settings kept");
    CHECK(E.lxr.dist.drive == 1.0f, "the selected FX has its own values");
}

/* Two instances never share state: interleaved == apart. */
static void test_instances(void) {
    static int16_t a[44096 * 2], b[44096 * 2], c[64 * 2];
    bk_engine_init(&E, 7);
    bk_engine_set_param(&E, BP_FX_TYPE, LXR_FX_DELAY);
    bk_engine_set_param(&E, BP_FX_MIX, 0.8f);
    bk_engine_set_param(&E, BP_WAVE, 4);                 /* noise: the RNG */
    bk_engine_set_param(&E, BP_LFO_AMT, 0.5f);
    bk_engine_set_param(&E, BP_LFO_DEST, 18);
    bk_engine_set_param(&E, BP_LFO_WAVE, 5);
    bk_engine_trigger(&E);
    render(&E, a, 44096);
    bk_engine_init(&E, 7);
    bk_engine_set_param(&E, BP_FX_TYPE, LXR_FX_DELAY);
    bk_engine_set_param(&E, BP_FX_MIX, 0.8f);
    bk_engine_set_param(&E, BP_WAVE, 4);
    bk_engine_set_param(&E, BP_LFO_AMT, 0.5f);
    bk_engine_set_param(&E, BP_LFO_DEST, 18);
    bk_engine_set_param(&E, BP_LFO_WAVE, 5);
    bk_engine_init(&E2, 99);
    bk_engine_set_param(&E2, BP_FX_TYPE, LXR_FX_PINGPONG);
    bk_engine_set_param(&E2, BP_WAVE, 4);
    bk_engine_trigger(&E);
    bk_engine_trigger(&E2);
    for (int i = 0; i < 44096; i += 64) {
        bk_engine_render(&E, b + i * 2);
        bk_engine_render(&E2, c);
        if (i % 2048 == 0) bk_engine_trigger(&E2);
    }
    CHECK(!memcmp(a, b, sizeof(a)), "a second instance changes nothing in the first");
}

/* ---- motion ------------------------------------------------------------- */
static void at(double pos16) { bk_motion_update(&M, &E, pos16); }

static void test_motion(void) {
    bk_engine_init(&E, 1);
    bk_motion_init(&M);
    CHECK(M.write == 0, "REC pad starts off");
    M.write = 1;
    int cut = bk_motion_lane(BP_CUTOFF), pit = bk_motion_lane(BP_PITCH);
    CHECK(bk_motion_lane(BP_VOLUME) < 0, "VOLUME is never automated");
    CHECK(cut >= 0 && pit >= 0, "lanes");

    /* untouched: nothing is written, whatever the knob does */
    for (double p = 0; p < 16; p += 0.05) { bk_engine_set_param(&E, BP_CUTOFF, (float)(p / 16)); at(p); }
    CHECK(bk_motion_mask(&M) == 0, "no touch, no automation");

    /* touch at 1/16 #2, sweep the knob 0.2 -> 0.8 across one 1/16, release */
    bk_engine_set_param(&E, BP_CUTOFF, 0.2f);
    at(2.0);
    bk_motion_touch(&M, &E, cut, 1);
    CHECK(bk_motion_recording(&M, &E) == (1u << cut), "recording while touched + running");
    for (int i = 0; i <= 64; ++i) {
        double p = 2.0 + i / 64.0;
        bk_engine_set_param(&E, BP_CUTOFF, 0.2f + 0.6f * (float)(i / 64.0));
        at(p);
        CHECK(isnan(E.ovr[BP_CUTOFF]), "you hear the hand while touching");
    }
    bk_motion_touch(&M, &E, cut, 0);
    bk_engine_set_param(&E, BP_CUTOFF, 0.5f);          /* knob left elsewhere */
    CHECK(bk_motion_mask(&M) == (1u << cut), "one lane");
    /* written: points 16..24 (#2 and the first point of #3), nothing else */
    int written = 0;
    for (int i = 0; i < BK_POINTS; ++i) written += M.pt[cut][i] != BK_EMPTY;
    CHECK(written == 9, "touch time = write time: %d points", written);
    /* playback retraces the sweep INSIDE the 1/16 (not a staircase) */
    at(2.5);
    CHECK(fabsf(bk_engine_value(&E, BP_CUTOFF) - 0.5f) < 0.02f, "mid-step curve: %.3f", bk_engine_value(&E, BP_CUTOFF));
    at(2.25);
    CHECK(fabsf(bk_engine_value(&E, BP_CUTOFF) - 0.35f) < 0.02f, "quarter-step: %.3f", bk_engine_value(&E, BP_CUTOFF));
    at(5.0);
    CHECK(isnan(E.ovr[BP_CUTOFF]) && bk_engine_value(&E, BP_CUTOFF) == 0.5f, "unwritten region plays the knob");
    at(16 + 2.5);                                      /* next loop (1 bar) */
    CHECK(fabsf(bk_engine_value(&E, BP_CUTOFF) - 0.5f) < 0.02f, "loops every bar");
    CHECK(E.param[BP_CUTOFF] == 0.5f, "playback never writes the knob");

    /* PITCH, step input: a value held across #4 and #5 is the value AT each hit */
    at(4.0);
    bk_motion_touch(&M, &E, pit, 1);
    bk_engine_set_param(&E, BP_PITCH, 20.0f);
    at(4.0);
    CHECK(M.pt[pit][32] == bk_motion_q(7.0f / 48.0f), "the step already started keeps its start value");
    at(4.5);
    at(5.0);
    bk_motion_touch(&M, &E, pit, 0);
    bk_engine_set_param(&E, BP_PITCH, 3.0f);
    at(21.0);                                          /* #5 of the next loop, at its hit */
    CHECK(bk_engine_value(&E, BP_PITCH) == 20.0f, "step pitch at the hit, an exact scale note: %.4f",
          bk_engine_value(&E, BP_PITCH));
    bk_engine_trigger(&E);
    CHECK(E.lxr.v.osc.baseNote == bk_pitch_midi(20, 0, 0), "the hit plays the recorded pitch");
    at(20.0);
    CHECK(bk_engine_value(&E, BP_PITCH) == 7.0f, "#4 plays its start value");
    at(20.1);
    CHECK(bk_engine_value(&E, BP_PITCH) == 7.0f, "pitch lane holds, no interpolation");
    at(21.5);
    CHECK(isnan(E.ovr[BP_PITCH]), "released at #5's start: the rest of #5 is the knob's");

    /* touching an automated knob picks up what is playing: no jump */
    at(18.5);
    float playing = bk_engine_value(&E, BP_CUTOFF);
    bk_motion_touch(&M, &E, cut, 1);
    CHECK(fabsf(bk_engine_value(&E, BP_CUTOFF) - playing) < 1e-3f, "touch picks up the playing value");
    bk_motion_touch(&M, &E, cut, 0);

    /* jitter one point back must not "wrap" and rewrite the whole loop */
    int before_n = 0;
    for (int i = 0; i < BK_POINTS; ++i) before_n += M.pt[cut][i] != BK_EMPTY;
    at(30.0);
    bk_motion_touch(&M, &E, cut, 1);
    at(29.99);
    at(30.0);
    bk_motion_touch(&M, &E, cut, 0);
    written = 0;
    for (int i = 0; i < BK_POINTS; ++i) written += M.pt[cut][i] != BK_EMPTY;
    CHECK(written - before_n <= 1, "jitter is not a lap: %d new points", written - before_n);

    /* REC pad off: touch is read-only */
    M.write = 0;
    unsigned before = bk_motion_mask(&M);
    bk_motion_touch(&M, &E, bk_motion_lane(BP_FM_AMOUNT), 1);
    for (double p = 0; p < 16; p += 0.1) at(p);
    bk_motion_touch(&M, &E, bk_motion_lane(BP_FM_AMOUNT), 0);
    CHECK(bk_motion_mask(&M) == before, "REC pad off writes nothing");
    M.write = 1;

    /* stopped: touch records nothing, overrides are gone */
    at(-1);
    CHECK(isnan(E.ovr[BP_CUTOFF]) && isnan(E.ovr[BP_PITCH]), "stopped: knobs play");
    bk_motion_touch(&M, &E, bk_motion_lane(BP_DECAY), 1);
    CHECK(bk_motion_recording(&M, &E) == 0, "stopped: not recording");
    bk_motion_touch(&M, &E, bk_motion_lane(BP_DECAY), 0);

    /* LENGTH: a 1/2-bar loop wraps at 8 */
    bk_engine_set_param(&E, BP_LENGTH, 0);
    at(8 + 2.5);
    CHECK(fabsf(bk_engine_value(&E, BP_CUTOFF) - 0.5f) < 0.02f, "8-step loop");
    CHECK(M.step == 2, "step %d", M.step);

    /* a touch nobody restates ends by itself (screen closed mid-touch) */
    at(1.0);
    int fm = bk_motion_lane(BP_FM_AMOUNT);
    bk_motion_touch(&M, &E, fm, 1);
    for (int i = 0; i < 100; ++i) {
        bk_motion_age(&M, 128);
        if (i % 20 == 0) bk_motion_touch(&M, &E, fm, 1);    /* heartbeat every ~58 ms */
    }
    CHECK(M.touched & (1u << fm), "restated touch stays");
    bk_motion_age(&M, BK_TOUCH_TIMEOUT_SAMPLES + 1);
    CHECK(!(M.touched & (1u << fm)), "an unrestated touch times out");
    bk_motion_clear(&M, &E, fm);

    /* every main-page sound knob records; nothing else does */
    static const int lanes[] = {BP_DECAY, BP_P_ENV, BP_FM_AMOUNT, BP_MOD_DECAY, BP_CUTOFF, BP_DRIVE, BP_PITCH};
    for (unsigned i = 0; i < sizeof(lanes) / sizeof(lanes[0]); ++i)
        CHECK(bk_motion_lane(lanes[i]) >= 0, "main knob %d is a lane", lanes[i]);
    for (int p = BP_ROOT; p < BP_COUNT; ++p) CHECK(bk_motion_lane(p) < 0, "%s is not a lane", bk_params[p].key);
    int pe = bk_motion_lane(BP_P_ENV);
    at(3.0);
    bk_motion_touch(&M, &E, pe, 1);
    bk_engine_set_param(&E, BP_P_ENV, 0.8f);
    at(3.5);
    bk_motion_touch(&M, &E, pe, 0);
    bk_engine_set_param(&E, BP_P_ENV, 0.1f);
    at(8 + 3.5);
    CHECK(fabsf(bk_engine_value(&E, BP_P_ENV) - 0.8f) < 1e-3f, "P.ENV plays back");

    bk_motion_clear(&M, &E, cut);
    CHECK(!(bk_motion_mask(&M) & (1u << cut)) && isnan(E.ovr[BP_CUTOFF]), "clear one lane");
    bk_motion_clear(&M, &E, -1);
    CHECK(bk_motion_mask(&M) == 0, "clear all");
}

static void write_wav(const char *path) {
    /* 8 s at 120 BPM: the defaults; FM + pitch env; delay; a Phrygian line
     * through the drive FX */
    bk_engine_init(&E, 42);
    int n = roll(&E, buf, 2.0f, 120);
    bk_engine_set_param(&E, BP_FM_AMOUNT, 0.7f);
    bk_engine_set_param(&E, BP_P_ENV, 0.7f);
    bk_engine_set_param(&E, BP_MOD_DECAY, 0.6f);
    n += roll(&E, buf + 2 * n, 2.0f, 120);
    bk_engine_set_param(&E, BP_FX_TYPE, LXR_FX_DELAY);
    bk_engine_set_param(&E, BP_FX_MIX, 0.4f);
    n += roll(&E, buf + 2 * n, 2.0f, 120);
    bk_engine_set_param(&E, BP_FX_TYPE, LXR_FX_DIST);
    bk_engine_set_param(&E, BP_FX_MIX, 0.7f);
    static const float seq[8] = {6, 6, 15, 11, 6, 15, 6, 11};   /* C Phrygian note indices */
    for (int s = 0; s < 16 && n < BUF_FRAMES - 6000; ++s) {
        bk_engine_set_param(&E, BP_PITCH, seq[s % 8]);
        bk_engine_set_param(&E, BP_CUTOFF, 0.45f + 0.4f * (float)s / 16.0f);
        n += roll(&E, buf + 2 * n, 0.125f, 120);
    }
    FILE *f = fopen(path, "wb");
    if (!f) { failures++; return; }
    int bytes = n * 4;
    unsigned char h[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,
                           0x44,0xAC,0,0,0x10,0xB1,0x02,0,4,0,16,0,'d','a','t','a',0,0,0,0};
    int riff = 36 + bytes;
    memcpy(h + 4, &riff, 4);
    memcpy(h + 40, &bytes, 4);
    fwrite(h, 1, 44, f);
    fwrite(buf, 4, (size_t)n, f);
    fclose(f);
    printf("wrote %s (%.1f s, peak %d)\n", path, n / BK_SR, peak(buf, n));
}

/* CPU on this machine for the worst case: a roll with the heaviest FX. */
static void bench(void) {
    for (int type = 0; type < LXR_FX_TYPES; ++type) {
        bk_engine_init(&E, 1);
        bk_engine_set_param(&E, BP_FX_TYPE, (float)type);
        clock_t t0 = clock();
        int blocks = 44100 * 4 / BK_BLOCK;
        for (int b = 0; b < blocks; ++b) {
            if (b % 43 == 0) bk_engine_trigger(&E);
            bk_engine_render(&E, buf);
        }
        double us = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e6 / blocks * 2.0;
        printf("info: FX %d: %.1f us per 128-frame block on this machine (budget 2902 us)\n", type, us);
    }
}

int main(int argc, char **argv) {
    test_scale();
    test_clock();
    test_table();
    test_hit();
    test_pitch();
    test_roll_continuity();
    test_volume_and_release();
    test_extremes();
    test_delay_feedback();
    test_fx_switch();
    test_instances();
    test_motion();
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--wav")) write_wav(argv[i + 1]);
    }
    bench();
    if (failures) { printf("%d FAILED\n", failures); return 1; }
    printf("PASS: scale, clock, table, hit, pitch, roll continuity, volume/release, extremes, "
           "delay feedback, FX switch, instances, motion\n");
    return 0;
}
