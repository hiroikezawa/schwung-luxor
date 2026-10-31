/* LUXOR - the RANDOM pad: Phrase Gen's generator. GPL-3.0-only (LICENSE).
 *
 * The same generator and the same six source MIDIs as the Phrase Gen MIDI FX
 * (dsp/phrase/ is a copy of it, as oBraids carries one): each press writes a
 * variation of one source, never the one just used, mutation 50 %, no rests.
 * The phrase is returned as MIDI notes; the caller turns each into a PITCH
 * PITCH knob value (a scale note index) -- set on the knob, not quantized later. */
#ifndef BK_PHRASE_H
#define BK_PHRASE_H

#include <stdint.h>

typedef struct {
    uint32_t rng;
    int last_source;        /* -1 none yet */
    int prev[8];            /* the last first half, so a new one differs */
    int have_prev;
} bk_phrase;

/* Builds the shared, read-only pattern library once. Safe to call again. */
void bk_phrase_library_init(void);
void bk_phrase_init(bk_phrase *p, uint32_t seed);
/* Writes `len` (8 or 16) notes. Returns 1 on success, 0 if no library. */
int  bk_phrase_generate(bk_phrase *p, int len, uint32_t entropy, int *notes);

#endif
