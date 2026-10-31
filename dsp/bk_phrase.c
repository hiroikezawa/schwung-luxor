/* LUXOR - the RANDOM pad. GPL-3.0-only (LICENSE). See bk_phrase.h. */
#include "bk_phrase.h"

#include "phrase/pg_generator.h"
#include "phrase/pg_library.h"
#include "phrase/pg_rng.h"
#include "phrase/pg_scale.h"
#include "pg_patterns_data.h"      /* generated: PG_EMBEDDED_PATTERNS[] */

#include <string.h>

#define MUTATION 0.5f               /* Phrase Gen's default */

/* One library for every instance: it is only read after this. */
static pg_library_t g_lib;
static int g_lib_ready;

void bk_phrase_library_init(void) {
    if (g_lib_ready) return;
    pg_library_init(&g_lib, 0 /* C */, PG_MASK_PHRYGIAN, 8);
    for (int i = 0; i < PG_EMBEDDED_COUNT; ++i) {
        const pg_embedded_pattern_t *e = &PG_EMBEDDED_PATTERNS[i];
        pg_library_add(&g_lib, e->name, e->data, e->len, e->start_step);
    }
    pg_library_finalize(&g_lib);
    g_lib_ready = 1;
}

void bk_phrase_init(bk_phrase *p, uint32_t seed) {
    memset(p, 0, sizeof(*p));
    p->rng = seed ? seed : 0x5A17C0DEu;
    p->last_source = -1;
}

static int make_half(bk_phrase *p, pg_rng_t *rng, const pg_pattern_t *avoid,
                     int source, pg_pattern_t *out) {
    pg_gen_params_t gp;
    gp.mutation = MUTATION;
    gp.source = source;
    gp.avoid_source = p->last_source;
    gp.allow_rests = 0;
    return pg_generate(&g_lib.model, &gp, rng, avoid, out, NULL);
}

int bk_phrase_generate(bk_phrase *p, int len, uint32_t entropy, int *notes) {
    if (!g_lib_ready || g_lib.n <= 0) return 0;
    if (len != 16) len = 8;
    /* When the press happened keeps two sessions from producing the same
     * run of phrases -- Phrase Gen mixes its entropy the same way. */
    pg_rng_t rng;
    pg_rng_seed(&rng, p->rng ^ (entropy * 0x9E3779B1u));
    pg_pattern_t prev, a, b;
    pg_pattern_clear(&prev, 8);
    if (p->have_prev)
        for (int i = 0; i < 8; ++i) { prev.steps[i].note = (int8_t)p->prev[i]; prev.steps[i].vel = 100; prev.steps[i].gate = 100; }
    int src = make_half(p, &rng, p->have_prev ? &prev : NULL, -1, &a);
    if (src < 0) return 0;
    if (len == 16 && make_half(p, &rng, &a, src, &b) < 0) return 0;
    p->last_source = src;
    for (int i = 0; i < 8; ++i) {
        p->prev[i] = a.steps[i].note;
        notes[i] = a.steps[i].note;
        if (len == 16) notes[8 + i] = b.steps[i].note;
    }
    p->have_prev = 1;
    p->rng = pg_rng_next(&rng);
    return 1;
}
