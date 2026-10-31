/* LUXOR - 1/16-note clock follower (from FMDRUM). GPL-3.0-only (LICENSE). */
#include "bk_clock.h"
#include "bk_engine.h"
#include <math.h>

void bk_clock_init(bk_clock *c) {
    c->running = 0;
    c->last_abs = -1;
}

int bk_clock_advance(bk_clock *c, double beat, float bpm, int frames, int *offsets) {
    if (!(beat >= 0.0) || frames <= 0) {          /* stopped (or NaN) */
        c->running = 0;
        return 0;
    }
    if (!(bpm >= 20.0f && bpm <= 999.0f)) bpm = 120.0f;
    double spb = (double)BK_SR * 60.0 / (double)bpm;   /* samples per beat */
    int n = 0;
    int64_t cur = (int64_t)floor(beat * 4.0 + 1e-9);
    if (!c->running || cur < c->last_abs || cur > c->last_abs + 1) {
        c->running = 1;
        c->last_abs = cur;
        offsets[n++] = 0;
    }
    while (n < BK_CLOCK_MAX_EVENTS) {
        int64_t next = c->last_abs + 1;
        double off = ((double)next / 4.0 - beat) * spb;
        if (off >= (double)frames) break;
        int o = off <= 0.0 ? 0 : (int)off;
        if (o >= frames) break;
        c->last_abs = next;
        offsets[n++] = o;
    }
    return n;
}
