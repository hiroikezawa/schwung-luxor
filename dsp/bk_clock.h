/* LUXOR - 1/16-note clock follower (copied from FMDRUM's fmd_clock). GPL-3.0-only (LICENSE).
 *
 * Owns no clock: each block it is handed the host transport position in
 * beats (0.0 = the downbeat of Move's Play, < 0 = stopped) and the tempo, and
 * answers where in the block each 1/16 boundary falls. Drift-free by
 * construction and testable without a host. */
#ifndef BK_CLOCK_H
#define BK_CLOCK_H

#include <stdint.h>

#define BK_CLOCK_MAX_EVENTS 4

typedef struct {
    int running;
    int64_t last_abs;   /* last 1/16 boundary that fired */
} bk_clock;

void bk_clock_init(bk_clock *c);

/* Writes up to BK_CLOCK_MAX_EVENTS ascending sample offsets (0..frames-1).
 *  - a start (or a backwards jump, i.e. a new Play) fires at offset 0, so the
 *    downbeat is never lost to a block boundary;
 *  - a boundary fires exactly once, even if host jitter reports it again;
 *  - a forward gap (slot not rendered for a while) fires once, late, rather
 *    than replaying every skipped 1/16. */
int bk_clock_advance(bk_clock *c, double beat, float bpm, int frames, int *offsets);

/* After an advance, the n-th returned event is 1/16 number
 * last_abs - (count - 1 - n) since the transport's start. */

#endif
