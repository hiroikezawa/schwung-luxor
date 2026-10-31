/*
 * LUXOR -- reference renderer: the LXR firmware's OWN sound engine code,
 * compiled unmodified from third_party/lxr, rendering the scenes of
 * scenarios.h for voice 1 (DRUM 1) into the FX bus. GPL-3.0-only.
 *
 *   ref_main <scene> <out.raw>      interleaved stereo int16, SCENE_BLOCKS x 64 frames
 *
 * Unmodified here: DrumVoice.c (initDrumVoice, Drum_trigger,
 * calcDrumVoiceAsync, calcDrumVoiceSyncBlock), lfo.c, modulationNode.c,
 * MidiParser.c (every parameter goes in as the CC message the front panel
 * sends, through midiParser_ccHandler), ParameterArray.c, FxBus.c, the FX and
 * every DSP building block. Transcribed (the firmware's mixer.c reads GPIO):
 * the voice-1 statements of mixer_calcNextSampleBlock below.
 *
 * What the rest of the firmware would provide is stubbed: no front panel, no
 * sequencer, no UART/USB, no faders (1.0), and GetRngValue() is the same
 * xorshift32 the port uses, so noise is comparable sample for sample.
 */
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "Soundengine/FxBus.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/MIDI/ParameterArray.h"
#include "Soundengine/MIDI/CortexParser.h"
#include "Soundengine/MIDI/MidiVoiceControl.h"
#include "Soundengine/MIDI/Uart.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/Sequencer/clockSync.h"
#include "IO/adcPots.h"
#include "menu/menu.h"
#include "Parameters.h"
#include "scenarios.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- what the rest of the firmware would define ------------------------- */
uint8_t activeParameters[NUM_PARAMS];
float mixer_decimation_rate[7];
uint8_t mixer_audioRouting[6];
SnareVoice snareVoice;
CymbalVoice cymbalVoice;
HiHatVoice hatVoice;
volatile uint32_t systick_ticks;
uint8_t frontParser_activeTrack;
uint8_t menu_activePage;
PatternSet seq_patternSet;

static uint32_t g_rng = 1u;
uint32_t GetRngValue(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g_rng = x;
    return x;
}
static uint16_t g_bpm = 120;
uint16_t seq_getBpm(void) { return g_bpm; }
float adc_getFaderVolume(uint8_t idx) { (void)idx; return 1.f; }
void Snare_setPan(const uint8_t pan) { (void)pan; }
void HiHat_setPan(const uint8_t pan) { (void)pan; }
void Cymbal_setPan(const uint8_t pan) { (void)pan; }
void voiceControl_noteOn(uint8_t voice, uint8_t note, uint8_t vel) { (void)voice; (void)note; (void)vel; }
void uart_sendMidi(MidiMsg msg) { (void)msg; }
void uart_sendFrontpanelByte(uint8_t data) { (void)data; }
void usb_sendMidi(MidiMsg msg) { (void)msg; }
void seq_addNote(uint8_t trackNr, uint8_t vel, uint8_t note) { (void)trackNr; (void)vel; (void)note; }
uint8_t seq_getExtSync(void) { return 0; }
uint8_t seq_isRunning(void) { return 0; }
uint8_t seq_isTrackMuted(uint8_t trackNr) { (void)trackNr; return 0; }
void seq_midiNoteOff(uint8_t chan) { (void)chan; }
void seq_recordAutomation(uint8_t voice, uint8_t dest, uint8_t value) { (void)voice; (void)dest; (void)value; }
void seq_sendMidiNoteOn(const uint8_t channel, const uint8_t note, const uint8_t veloc) { (void)channel; (void)note; (void)veloc; }
void seq_setMute(uint8_t trackNr, uint8_t isMuted) { (void)trackNr; (void)isMuted; }
void seq_setNextPattern(const uint8_t patNr) { (void)patNr; }
void seq_setRunning(uint8_t isRunning) { (void)isRunning; }
void seq_sync(void) {}
void sync_midiResetPosition(void) {}
void sync_midiStartStop(uint8_t isStart) { (void)isStart; }
uint8_t menu_getActiveVoice(void) { return 0; }
uint8_t menu_getSubPage(void) { return 0; }
uint8_t menu_getViewedPattern(void) { return 0; }
void menu_repaint(void) {}
void menu_switchSubPage(uint8_t subPageNr, uint8_t toggle) { (void)subPageNr; (void)toggle; }

/* ---- parameters as CC messages ----------------------------------------- */
typedef struct { int cc2; int num; } cc_t;

static cc_t cc_of(int p) {
    switch (p) {
    case LXR_COARSE:       return (cc_t){0, F_OSC1_COARSE};
    case LXR_FINE:         return (cc_t){0, F_OSC1_FINE};
    case LXR_OSC_WAVE:     return (cc_t){0, OSC_WAVE_DRUM1};
    case LXR_PWM:          return (cc_t){1, CC2_PWM_1};
    case LXR_ATTACK:       return (cc_t){0, VELOA1};
    case LXR_DECAY:        return (cc_t){0, VELOD1};
    case LXR_VOL_SLOPE:    return (cc_t){0, VOL_SLOPE1};
    case LXR_PITCH_DECAY:  return (cc_t){0, PITCHD1};
    case LXR_PITCH_SLOPE:  return (cc_t){0, PITCH_SLOPE1};
    case LXR_MOD_AMOUNT:   return (cc_t){0, MODAMNT1};
    case LXR_FM_AMOUNT:    return (cc_t){0, FMAMNT1};
    case LXR_FM_FREQ:      return (cc_t){0, FMDTN1};
    case LXR_MOD_WAVE:     return (cc_t){0, MOD_WAVE_DRUM1};
    case LXR_MIX_MOD:      return (cc_t){1, CC2_MIX_MOD_1};
    case LXR_TRANS_WAVE:   return (cc_t){1, CC2_TRANS1_WAVE};
    case LXR_TRANS_VOL:    return (cc_t){1, CC2_TRANS1_VOL};
    case LXR_TRANS_FREQ:   return (cc_t){1, CC2_TRANS1_FREQ};
    case LXR_FILTER_FREQ:  return (cc_t){0, FILTER_FREQ_DRUM1};
    case LXR_RESO:         return (cc_t){0, RESO_DRUM1};
    case LXR_FILTER_TYPE:  return (cc_t){1, CC2_FILTER_TYPE_1};
    case LXR_FILTER_DRIVE: return (cc_t){1, CC2_FILTER_DRIVE_1};
    case LXR_LFO_FREQ:     return (cc_t){0, FREQ_LFO1};
    case LXR_LFO_SYNC:     return (cc_t){1, CC2_SYNC_LFO1};
    case LXR_LFO_AMOUNT:   return (cc_t){0, AMOUNT_LFO1};
    case LXR_LFO_WAVE:     return (cc_t){1, CC2_WAVE_LFO1};
    case LXR_LFO_RETRIG:   return (cc_t){1, CC2_RETRIGGER_LFO1};
    case LXR_LFO_OFFSET:   return (cc_t){1, CC2_OFFSET_LFO1};
    case LXR_VOL:          return (cc_t){0, VOL1};
    case LXR_PAN:          return (cc_t){0, PAN1};
    case LXR_DECIMATION:   return (cc_t){0, VOICE_DECIMATION1};
    case LXR_DRIVE:        return (cc_t){0, OSC1_DIST};
    case LXR_FX_P1:        return (cc_t){1, CC2_FX_P1};
    case LXR_FX_P2:        return (cc_t){1, CC2_FX_P2};
    case LXR_FX_P3:        return (cc_t){1, CC2_FX_P3};
    case LXR_FX_P4:        return (cc_t){1, CC2_FX_P4};
    }
    fprintf(stderr, "no CC for %d\n", p);
    exit(2);
}

/* The parameterArray / activeParameters index of a CC (modNode_updateValue's
 * "stupid offset": CC n is n-1, CC2 n is 128+n). */
static int dest_of(cc_t c) { return c.cc2 ? 128 + c.num : c.num - 1; }

static void send(cc_t c, int value) {
    MidiMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.status = c.cc2 ? MIDI_CC2 : MIDI_CC;
    msg.data1 = (uint8_t)c.num;
    msg.data2 = (uint8_t)value;
    activeParameters[dest_of(c)] = (uint8_t)value;    /* the front panel's copy */
    midiParser_ccHandler(msg, 1, 0, 0.f);
    modNode_originalValueChanged((uint16_t)dest_of(c));
}

static void r_set(int p, int v) { send(cc_of(p), v); }
static void r_lfo_target(int p) {
    modNode_setDestination(&voiceArray[0].lfo.modTarget, p < 0 ? 0 : (uint16_t)dest_of(cc_of(p)));
}
static void r_fx_type(int type) {
    if (type == LXR_FX_PINGPONG) {
        send((cc_t){1, CC2_FX_DELAY_TYPE}, DELAY_PINGPONG);
        type = FX_DELAY;
    }
    send((cc_t){1, CC2_FX_TYPE}, type);
}
static void r_fx_param(int idx, int v) { send(cc_of(LXR_FX_P1 + idx), v); }
static void r_dry_wet(int pc) { send((cc_t){1, CC2_FX_AMOUNT}, pc); }
static void r_dist_type(int t) { send((cc_t){1, CC2_FX_DIST_TYPE}, t); }
static void r_ratio(int r) { send((cc_t){1, CC2_FX_RATIO}, r); }
static void r_ring_wave(int w) { send((cc_t){1, CC2_FX_WAVE}, w); }
static void r_bpm(float bpm) { g_bpm = (uint16_t)bpm; lfo_recalcSync(); }

/* ---- mixer.c mixer_calcNextSampleBlock, voice 1 only ------------------- */
static void render_block(int16_t *out)
{
	modNode_resetTargets();
	modNode_reassignVeloMod();
	lfo_dispatchNextValue(&voiceArray[0].lfo);
	SVF_recalcFreq(&voiceArray[0].filter);
	calcDrumVoiceAsync(0);

	int16_t sampleData[OUTPUT_DMA_SIZE];
	int16_t fxBusData[OUTPUT_DMA_SIZE*2];
	int16_t output[OUTPUT_DMA_SIZE*2], output2[OUTPUT_DMA_SIZE*2];
	memset(output, 0, sizeof(output));
	memset(output2, 0, sizeof(output2));
	memset(fxBusData, 0, sizeof(fxBusData));

	calcDrumVoiceSyncBlock(0, sampleData,OUTPUT_DMA_SIZE);
	/* mixer_decimateBlock(0,sampleData) */
	static float cnt;
	static int16_t held;
	float phaseInc = mixer_decimation_rate[0]*mixer_decimation_rate[6];
	for(int i=0;i<OUTPUT_DMA_SIZE;i++)
	{
		cnt += phaseInc;
		if(cnt >= 1.f)
		{
			cnt -= 1.f;
			held = sampleData[i];
		}
		sampleData[i] = held;
	}
	/* mixer_addDataToOutput(MIXER_ROUTING_FX_BUS, triggeredPanL, triggeredPanR) */
	int16_t *fxBuffer = fxBusData;
	for(int i=0;i<OUTPUT_DMA_SIZE;i++)
	{
		int32_t y = *((int32_t*)fxBuffer);
		int32_t x = __PKHBT( (int16_t)(sampleData[i] * voiceArray[0].triggeredPanL), (int16_t)(sampleData[i] * voiceArray[0].triggeredPanR), 16);
		*((int32_t*)fxBuffer) = __QADD16(x, y);
		fxBuffer += 2;
	}
	FxBus_process(fxBusData, &output[0], &output[1], &output2[0], &output2[1], OUTPUT_DMA_SIZE);
	memcpy(out, output2, sizeof(output2));
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: ref_main <scene> <out.raw>\n"); return 2; }
    const int s = atoi(argv[1]);
    g_rng = 0x1234567u + (uint32_t)s;
    parameterArray_init();
    initDrumVoice();
    for (int i = 0; i < 7; ++i) mixer_decimation_rate[i] = 1;      /* mixer_init */
    mixer_audioRouting[0] = MIXER_ROUTING_FX_BUS;
    FxBus_init();
    const scene_api api = {r_set, r_lfo_target, r_fx_type, r_fx_param, r_dry_wet,
                           r_dist_type, r_ratio, r_ring_wave, r_bpm};
    scene_setup(s, &api);
    FILE *f = fopen(argv[2], "wb");
    if (!f) return 1;
    int16_t out[OUTPUT_DMA_SIZE * 2];
    for (int b = 0; b < SCENE_BLOCKS; ++b) {
        uint8_t note;
        if (scene_triggers(s, b, &note)) Drum_trigger(0, 127, note);
        render_block(out);
        fwrite(out, sizeof(out), 1, f);
    }
    fclose(f);
    return 0;
}
