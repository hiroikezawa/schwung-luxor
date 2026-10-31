/*
 * LUXOR -- the scenes of scenarios.h through the per-instance port
 * (dsp/lxr/lxr_drum.c). GPL-3.0-only.
 *
 *   port_main <scene> <out.raw>
 *
 * Same output format as ref_main. Two port features that the firmware does
 * not have are held still so the comparison is exact: the FX type change
 * crossfade (the scenes select their FX before the first sample), and
 * nothing else differs.
 */
#include "lxr_drum.h"
#include "scenarios.h"
#include <stdio.h>
#include <stdlib.h>

static lxr_drum_t t;

static void p_set(int p, int v) { lxr_drum_set_index(&t, p, v); }
static void p_lfo_target(int p) {
    for (int i = 0; i < lxr_lfo_target_count; ++i)
        if (lxr_lfo_targets[i] == p) { lxr_drum_set_lfo_target(&t, i); return; }
    fprintf(stderr, "no LFO target %d\n", p);
    exit(2);
}
static void p_fx_type(int type) { lxr_drum_set_fx_type(&t, type); t.fx_fade = 0; }
static void p_fx_param(int idx, int v) { lxr_drum_set_index(&t, LXR_FX_P1 + idx, v); }
static void p_dry_wet(int pc) { lxr_drum_set_dry_wet(&t, pc / 100.f); }
static void p_dist_type(int ty) { lxr_drum_set_dist_type(&t, ty); }
static void p_ratio(int r) { lxr_drum_set_ratio(&t, r); }
static void p_ring_wave(int w) { lxr_drum_set_ring_wave(&t, w); }
static void p_bpm(float bpm) { lxr_drum_set_bpm(&t, bpm); }

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: port_main <scene> <out.raw>\n"); return 2; }
    const int s = atoi(argv[1]);
    lxr_drum_init(&t, 0x1234567u + (uint32_t)s);
    const scene_api api = {p_set, p_lfo_target, p_fx_type, p_fx_param, p_dry_wet,
                           p_dist_type, p_ratio, p_ring_wave, p_bpm};
    scene_setup(s, &api);
    FILE *f = fopen(argv[2], "wb");
    if (!f) return 1;
    int16_t out[LXR_BLOCK * 2];
    for (int b = 0; b < SCENE_BLOCKS; ++b) {
        uint8_t note;
        if (scene_triggers(s, b, &note)) lxr_drum_trigger(&t, note);
        lxr_drum_render(&t, out);
        fwrite(out, sizeof(out), 1, f);
    }
    fclose(f);
    return 0;
}
