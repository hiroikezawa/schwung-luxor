/*
 * lxr_drum.c -- one LXR DRUM voice and the LXR FX bus, per instance.
 *
 * Copyright (C) 2012-2026 Erica Synths
 * Modified 2026 by Hiro Ikezawa for LUXOR (Schwung / Ableton Move).
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of a port of the LXR firmware's sound engine, licensed
 * under the GNU General Public License version 3 with the additional terms in
 * third_party/lxr/NOTICE.md. It is NOT Erica Synths software and is not
 * endorsed by Erica Synths.
 *
 * Every function below is a copy of the firmware function named in its
 * comment (third_party/lxr/Core/..., vendored at UPSTREAM_COMMIT). The bodies
 * are the original statements; what changed is listed at each one and is
 * always the same kind of change: a global (voiceArray[voiceNr],
 * velocityModulators[], fxBus, mixer_decimation_*) became the instance `t`.
 * No arithmetic, constant, or order of operations was altered.
 */
#include "lxr_drum.h"
#include "Soundengine/DSPAudio/random.h"
#include "Soundengine/DSPAudio/BufferTools.h"
#include "Soundengine/MIDI/valueShaper.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/DSPAudio/mixer.h"
#include <math.h>
#include <string.h>

/* ---- the two hardware services ---------------------------------------------
 * GetRngValue() is the STM32 RNG peripheral (random.c); seq_getBpm() is the
 * LXR sequencer's tempo. Both are read by unmodified vendored code (noise
 * oscillator, LFO noise, LFO sync), so they are plain functions answering for
 * the instance being processed -- set at every entry point below, and every
 * entry point runs on the one audio thread. The RNG is a per-instance
 * xorshift32: the hardware's is a true RNG, so no two runs of the original
 * agree either; this one keeps instances apart and tests repeatable. */
static lxr_drum_t *g_cur;

uint32_t GetRngValue(void) {
    uint32_t x = g_cur ? g_cur->rng : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    if (g_cur) g_cur->rng = x;
    return x;
}

float seq_getBpm(void) { return g_cur ? g_cur->bpm : 120.f; }

/* ---- MidiParser.c: midiParser_calcDetune, verbatim ------------------------ */
// 2^(1/12) factor for 1 semitone
#define SEMITONE_UP 1.0594630943592952645618252949463f
#define SEMITONE_DOWN 0.94387431268169349664191315666753f
//takes a midi value from 0 to 127 and return +/- 50 cent detune factor
float midiParser_calcDetune(uint8_t value)
{
	//linear interpolation between 1(no change) and semitone up/down)
	float frac = (value/127.f -0.5f);
	float cent = 1;
	if(cent>=0)
	{
		cent += frac*(SEMITONE_UP - 1);
	}
	else
	{
		cent += frac*(SEMITONE_UP - 1);
	}
	return cent;
}

/* MidiParser.c calcPitchModAmount, verbatim. */
static inline float calcPitchModAmount(uint8_t data2)
{
	const float val = data2/127.f;
	return val*val*PITCH_AMOUNT_FACTOR;
}

/* ---- lfo.c ------------------------------------------------------------------
 * lfo_calc, lfo_calcPhaseInc, lfo_setFreq and lfo_setSync are verbatim.
 * lfo_init no longer calls modNode_init (which re-targets every global
 * modulator); it sets the same initial target, none, amount 0. */
void lfo_init(Lfo *lfo)
{
	lfo->phase 			= 0;
	lfo->phaseInc 		= 1;
	lfo->phaseOffset	= 0;
	lfo->retrigger		= 0;
	lfo->waveform		= SINE;
	lfo->sync			= 0;
	lfo->freq			= 1;
	lfo->modNodeValue	= 1;
	lfo->modTarget.lastVal = 0;
	lfo->modTarget.amount = 0.f;
	lfo->modTarget.destination = 0;
}
//-------------------------------------------------------------
static float lfo_calc(Lfo *lfo)
{
	uint32_t oldPhase 		= lfo->phase;
	float inc = lfo->phaseInc*lfo->modNodeValue;
	uint32_t incInt = inc;
	lfo->phase 			   += incInt;//lfo->phaseInc*lfo->modNodeValue;
	const uint8_t overflow 	= oldPhase>lfo->phase;
	switch(lfo->waveform)
	{
	//---
		case LFO_SINE:
			return ((sine_table[(lfo->phase>>20)]/32767.f) + 1)/2.f;
		break;
	//---
		case LFO_TRI:
			return (1.f-fabsf( (lfo->phase/(float)0xffffffff)*2-1 ) );
			break;
	//---
		case LFO_SAW_UP:
		return lfo->phase/(float)0xffffffff ;
		break;
	//---
		case LFO_REC:
		if(lfo->phase > 0x7fffffff)
		{
			return 1.f;
		}
		else
		{
			return 0.f;
		}
		break;
	//---
		case LFO_NOISE:
		if(overflow)
		{
			lfo->rnd = GetRngValue();
			lfo->rnd = lfo->rnd  / (float)0xffffffff ;
		}
			return lfo->rnd;
		break;
	//---
		case LFO_SAW_DOWN:
		return (0xffffffff-(lfo->phase)) / (float)0xffffffff;
		break;
	//---
		case LFO_EXP_UP:
			{
				float x = lfo->phase/(float)0xffffffff;
				return x*x*x;
			}
			break;
	//---
		case LFO_EXP_DOWN:
			{
				float x = (0xffffffff-(lfo->phase)) / (float)0xffffffff;
				return x*x*x;//valueShaperF2F(x,-0.9f);
			}
			break;
	//---
		default:
		return 0;
		break;
	}
	return 0;
}
//-------------------------------------------------------------
static uint32_t lfo_calcPhaseInc(float freq, uint8_t sync)
{
	if(sync==0)//no sync
	{
		return freq/(LFO_SR) * 0xffffffff;
	}
	//sync is on
	float bpm =  seq_getBpm();
	float tempoAsFrequency = bpm > 0.0 ? bpm / 60.f /4: 0.0f; //bpm/60 = beat duration /4 = bar duration
	float scaler;
	switch(sync)
	{
		default: //no sync
		return freq/(LFO_SR) * 0xffffffff;
		break;
		case 1: // 4/1
		scaler = 0.25f;
		break;
		case 2: // 2/1
		scaler = 0.5f;
		break;
		case 3: // 1/1
		scaler = 1;
		break;
		case 4: // 1/2
		scaler = 2;
		break;
		case 5: //1/3
		scaler = 3;
		break;
		case 6: // 1/4
		scaler = 4;
		break;
		case 7: // 1/6
		scaler = 6;
		break;
		case 8: // 1/8
		scaler = 8;
		break;
		case 9: // 1/12
		scaler = 12;
		break;
		case 10: // 1/16
		scaler = 16;
		break;
		case 11: // 1/32
		scaler = 32;
		break;
	}
	float lfoSyncfreq = tempoAsFrequency * scaler;
	return (lfoSyncfreq / (LFO_SR)) * 0xffffffff;
}
//-------------------------------------------------------------
void lfo_setFreq(Lfo *lfo, float f)
{
	f = 0.0078125f + f* (1.f-0.0078125f); //to prevent f from being zero
	f = f*f*f;
	lfo->freq = f*LFO_MAX_F;
	lfo->phaseInc = lfo_calcPhaseInc(lfo->freq,lfo->sync);
}
//-------------------------------------------------------------
void lfo_setSync(Lfo* lfo, uint8_t sync)
{
	lfo->sync = sync;
	lfo->phaseInc = lfo_calcPhaseInc(lfo->freq,lfo->sync);
}

/* ---- LFO destinations --------------------------------------------------------
 * Cc2Text.c modTargets[], the "Voice 1 - Drum 1" block in its order, minus
 * the velocity rows, the LFO's own rows and the voice-wide sample rate. */
const int lxr_lfo_targets[] = {
    -1,
    LXR_COARSE, LXR_FINE, LXR_OSC_WAVE, LXR_PWM,
    LXR_ATTACK, LXR_DECAY, LXR_VOL_SLOPE,
    LXR_PITCH_DECAY, LXR_PITCH_SLOPE, LXR_MOD_AMOUNT,
    LXR_FM_AMOUNT, LXR_FM_FREQ, LXR_MOD_WAVE, LXR_MIX_MOD,
    LXR_TRANS_WAVE, LXR_TRANS_VOL, LXR_TRANS_FREQ,
    LXR_FILTER_FREQ, LXR_RESO, LXR_FILTER_TYPE, LXR_FILTER_DRIVE,
    LXR_VOL, LXR_PAN, LXR_DECIMATION, LXR_DRIVE,
    LXR_FX_P1, LXR_FX_P2, LXR_FX_P3, LXR_FX_P4,
};
const int lxr_lfo_target_count = (int)(sizeof(lxr_lfo_targets) / sizeof(lxr_lfo_targets[0]));

/* ---- FxBus.c ------------------------------------------------------------------
 * FxBus_updateParameter for one FX type, reading that type's own four values
 * (the firmware has one parameter[4] shared by whichever FX is selected, and
 * LUXOR keeps each FX's settings across a type change). Statements as the
 * original's switch. */
static void fx_update(lxr_drum_t *t, int type)
{
	const float *p = t->fx_par[type];
	switch(type)
	{
	case LXR_FX_DIST:
		Distortion_setDrive(&t->dist, p[0] );
		Distortion_setParam1(&t->dist, p[1] );
		Distortion_setParam2(&t->dist, p[2] );
		Distortion_setParam3(&t->dist, p[3] );
		break;

	case LXR_FX_RING:
		Ringmod_setFreq(&t->ring, p[0] );
		break;

	case LXR_FX_COMP:
		Compressor_setAttack(&t->comp , p[0]);
		Compressor_setRelease(&t->comp , p[1]);
		Compressor_setTreshold(&t->comp , p[2]);
		Compressor_setGain(&t->comp , p[3]);
		break;

	case LXR_FX_DELAY:
	case LXR_FX_PINGPONG:
	{
		Delay *d = &t->delay[type == LXR_FX_PINGPONG].d;
		Delay_setRange(d, p[1]);
		Delay_setDelayTime(d, p[0]);
		Delay_setFilter(d, p[2]);
		Delay_setFeedback(d, p[3]);
	}
		break;
	default:
		break;
	}
}

/* FxBus_process with the firmware's routing fixed at MIXER_ROUTING_DAC1_STEREO
 * (two channels into one interleaved buffer). `out` must be zeroed: every FX
 * ADDS into it, as into the firmware's cleared DMA buffer. */
static void fx_process(lxr_drum_t *t, int type, int16_t *fxBuffer, int16_t *out, uint32_t size)
{
	const uint8_t channels = 2;
	switch(type)
	{
	default:
	case LXR_FX_OFF:
	{
		int32_t* stereoIn = (int32_t*)fxBuffer;
		int32_t* stereoOut = (int32_t*)out;
		for(uint8_t i=0;i<size;i++)
		{
			//add stereo samples using SIMD
			*stereoOut = __QADD16(*stereoOut, *stereoIn);
			stereoOut++;
			stereoIn++;
		}
	}
	break;

	case LXR_FX_DIST:
		Distortion_process(&t->dist, fxBuffer, out, out + 1, size, channels, t->dry_wet);
		break;

	case LXR_FX_RING:
		Ringmod_process(&t->ring, fxBuffer, out, out + 1, size, channels, t->dry_wet);
		break;

	case LXR_FX_COMP:
		Compressor_process(&t->comp, fxBuffer, out, out + 1, size, channels, t->dry_wet);
		break;

	case LXR_FX_DELAY:
		Delay_process(&t->delay[0].d, fxBuffer, out, out + 1, size, channels, t->dry_wet);
		break;

	case LXR_FX_PINGPONG:
		Delay_processPingPong(&t->delay[1].d, fxBuffer, out, out + 1, size, channels, t->dry_wet);
		break;
	}
}

/* ---- MidiParser.c midiParser_ccHandler: the DRUM 1 and FX bus cases ----------
 * `itg`/`flt`/`isFloatModulator` are the original's value.itg / value.flt /
 * isFloatModulator: a knob's value enters as a float modulator would (the
 * parser's own continuous path, itg = v*127), a menu choice as a CC value. */
static void apply(lxr_drum_t *t, int param, uint8_t itg, float flt, uint8_t isFloatModulator)
{
	DrumVoice *v = &t->v;
	switch(param)
	{
		case LXR_VOL_SLOPE:
			slopeEg2_setSlope(&v->oscVolEg,itg);
			break;
		case LXR_PITCH_SLOPE:
			DecayEg_setSlope(&v->oscPitchEg,itg);
			break;
		case LXR_FILTER_FREQ:
		{
			const float f = flt;
			//exponential full range freq
			SVF_directSetFilterValue(&v->filter,valueShaperF2F(f,FILTER_SHAPER) );
		}
			break;
		case LXR_RESO:
			SVF_setReso(&v->filter, flt);
			break;
		case LXR_COARSE:
			//clear upper nibble
			v->osc.midiFreq &= 0x00ff;
			//set upper nibble
			v->osc.midiFreq |= itg << 8;
			osc_recalcFreq(&v->osc);
			break;
		case LXR_FINE:
			//clear lower nibble
			v->osc.midiFreq &= 0xff00;
			//set lower nibble
			v->osc.midiFreq |= itg;
			osc_recalcFreq(&v->osc);
			break;
		case LXR_OSC_WAVE:
			if(isFloatModulator)
			{
				uint8_t wave = NUM_OSC_WAVEFORMS * flt;
				wave = wave >= NUM_OSC_WAVEFORMS ? NUM_OSC_WAVEFORMS-1 : wave;
				v->osc.waveform = wave;
			}
			else
			{
				v->osc.waveform = itg;
			}
			break;
		case LXR_MOD_WAVE:
			if(isFloatModulator)
			{
				uint8_t wave = NUM_OSC_WAVEFORMS * flt;
				wave = wave >= NUM_OSC_WAVEFORMS ? NUM_OSC_WAVEFORMS-1 : wave;
				v->modOsc.waveform = wave;
			}
			else
			{
				v->modOsc.waveform = itg;
			}
			break;
		case LXR_DRIVE:
			setDistortionShape(&v->distortion,flt);
			break;
		case LXR_ATTACK:
			slopeEg2_setAttack(&v->oscVolEg,itg,AMP_EG_SYNC);
			break;
		case LXR_DECAY:
			slopeEg2_setDecay(&v->oscVolEg,itg,AMP_EG_SYNC);
			break;
		case LXR_PITCH_DECAY:
			DecayEg_setDecay(&v->oscPitchEg,itg);
			break;
		case LXR_MOD_AMOUNT:
			v->egPitchModAmount = calcPitchModAmount(itg);
			break;
		case LXR_FM_AMOUNT:
			v->fmModAmount = flt;
			break;
		case LXR_FM_FREQ:
			//clear upper nibble
			v->modOsc.midiFreq &= 0x00ff;
			//set upper nibble
			v->modOsc.midiFreq |= itg << 8;
			osc_recalcFreq(&v->modOsc);
			break;
		case LXR_VOL:
			v->vol = flt;
			break;
		case LXR_PAN:
			v->pan = itg;                       /* setPan(0,value.itg) */
			break;
		case LXR_LFO_FREQ:
			lfo_setFreq(&v->lfo,flt);
			break;
		case LXR_LFO_AMOUNT:
			v->lfo.modTarget.amount = flt;
			break;
		case LXR_DECIMATION:
			t->v.decimationRate = valueShaperF2F(flt,-0.7f);   /* mixer_decimation_rate[0] */
			break;
		case LXR_TRANS_WAVE:
			if(isFloatModulator)
			{
				uint8_t wave = NUM_TRANSIENTS * flt;
				wave = wave >= NUM_TRANSIENTS ? NUM_TRANSIENTS-1 : wave;
				transient_setWaveform(&v->transGen, wave);
			}
			else
			{
				transient_setWaveform(&v->transGen, itg);
			}
			break;
		case LXR_TRANS_VOL:
			v->transGen.volume = flt;
			break;
		case LXR_TRANS_FREQ:
			v->transGen.pitch = 1.f + ((itg/33.9f)-0.75f) ;// range about  0.25 to 4 => 1/4 to 1*4
			break;
		case LXR_FILTER_TYPE:
			if(isFloatModulator)
			{
				uint8_t type = FILTER_NAIVE_2_POLE * flt + 1;
				type = type >= FILTER_OFF ? FILTER_OFF-1 : type;
				v->filterType = type;
			}
			else
			{
				v->filterType = itg+1;
			}
			break;
		case LXR_FILTER_DRIVE:
			SVF_setDrive(&v->filter, flt);
			break;
		case LXR_MIX_MOD:
			v->mixOscs = itg != 0;
			break;
		case LXR_LFO_WAVE:
			if(isFloatModulator)
			{
				uint8_t wave = NUM_LFO_WAVEFORMS * flt;
				wave = wave >= NUM_LFO_WAVEFORMS ? NUM_LFO_WAVEFORMS-1 : wave;
				v->lfo.waveform = wave;
			}
			else
			{
				v->lfo.waveform = itg;
			}
			break;
		case LXR_LFO_RETRIG:
			if(isFloatModulator)
			{
				uint8_t voice = 7 * flt;
				v->lfo.retrigger = voice;
			}
			else
			{
				v->lfo.retrigger = itg;
			}
			break;
		case LXR_LFO_SYNC:
			lfo_setSync(&v->lfo, itg);
			break;
		case LXR_LFO_OFFSET:
			v->lfo.phaseOffset = flt * 0xffffffff;
			break;
		case LXR_PWM:
			v->pwm = flt;
			break;
		case LXR_FX_P1:
		case LXR_FX_P2:
		case LXR_FX_P3:
		case LXR_FX_P4:
			/* FxBus_setParameter(idx, value.flt) */
			t->fx_par[t->fx_type][param - LXR_FX_P1] = flt;
			fx_update(t, t->fx_type);
			break;
		default:
			break;
	}
}

static void apply_float(lxr_drum_t *t, int param, float v)
{
	v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
	apply(t, param, (uint8_t)(v * 127), v, 1);
}

/* modulationNode.c modNode_resetTargets for this voice's LFO: put the target
 * back at its original value before the LFO moves it again. The firmware does
 * it with paramArray_setParameter(), i.e. through the parser again with the
 * stored CC value; a value set by a knob goes back the way it came. */
static void lfo_reset_target(lxr_drum_t *t)
{
	const int p = lxr_lfo_targets[t->lfo_target];
	if (p < 0) return;
	if (t->param_cc[p] >= 0) apply(t, p, (uint8_t)t->param_cc[p], t->param_cc[p] / 127.f, 0);
	else apply_float(t, p, t->param[p]);
}

/* lfo.c lfo_dispatchNextValue + modulationNode.c modNode_updateValue. The
 * original value is the target's activeParameters[] / 127 -- here the base
 * value, which IS that ratio. */
static void lfo_dispatch(lxr_drum_t *t)
{
	Lfo *lfo = &t->v.lfo;
	float val = lfo_calc(lfo);
	ModulationNode *vm = &lfo->modTarget;
	vm->lastVal = val;
	const int p = lxr_lfo_targets[t->lfo_target];
	if (p < 0) return;
	float originalValue = t->param[p];
	//val is [0:1] make [-1:1] for bipolar modulation around original value
	float newValue = originalValue + vm->amount * (val*2.f-1.f);
	newValue = newValue < 0.f ? 0 : newValue > 1.f ? 1.f : newValue;
	apply(t, p, (uint8_t)(newValue*127), newValue, 1);
}

/* ---- DrumVoice.c --------------------------------------------------------------
 * initDrumVoice's loop body for voice 0, then FxBus_init and mixer_init's
 * share. `voiceArray[i]` -> `t->v`; setPan(i, 0) is the field write it was. */
void lxr_drum_init(lxr_drum_t *t, uint32_t seed)
{
	memset(t, 0, sizeof(*t));
	for (int p = 0; p < LXR_PARAM_COUNT; ++p) t->param_cc[p] = -1;
	t->rng = seed ? seed : 0x5EEDu;
	t->bpm = 120.f;
	g_cur = t;
	const int i = 0;
	DrumVoice *v = &t->v;

		initOnePole(&v->FaderVolumeFilter);
		setOnePoleCoef(&v->FaderVolumeFilter,0.01);

#if SMOOTH_MIXER_VOL_PAN_DRIVE
		for(uint8_t j=0;j<2;j++)
		{
			initOnePole(&v->MixerPageFilter[j]);
			setOnePoleCoef(&v->MixerPageFilter[j],0.01);
		}
		initOnePole(&v->distortion.DriveParamFilter);
		setOnePoleCoef(&v->distortion.DriveParamFilter,0.01);

#endif

		SnapEg_init(&v->snapEg);
		v->pan = 0;
		v->vol = 0.8f;
		v->fmModAmount = 0.5f;
		transient_init(&v->transGen);
#if ENABLE_DRUM_SVF
		SVF_init(&v->filter);
		v->filterType = 0x01;
#endif
		lfo_init(&v->lfo);

		v->modOsc.freq = 440;
		v->modOsc.waveform = 1;
		v->modOsc.fmMod = 0;
		v->modOsc.midiFreq = 70<<8 | 64;
		v->modOsc.pitchMod = 1.0f;
		v->modOsc.modNodeValue = 1;

		v->volumeMod = 1;

		v->osc.freq = 440;
		v->osc.modNodeValue = 1;
		v->osc.waveform = TRI+i; //for testing init to tri,saw,rec
		v->osc.fmMod = 0;
		v->osc.midiFreq = 70<<8 | 64;

		DecayEg_init(&v->oscPitchEg);
		v->egPitchModAmount = 0.5f;

		slopeEg2_init(&v->oscVolEg);
		setDistortionShape(&v->distortion, 0.f);

#if ENABLE_MIX_OSC
		v->mixOscs = true;
#endif
		v->decimationCnt = 0;
		v->decimationRate = 1;

	/* mixer_init: decimation rate 1, counter and held sample 0 (the rate
	 * itself lives in v->decimationRate, a field DrumVoice already has and
	 * the firmware leaves unused). */
	t->dec_cnt = 0;
	t->dec_sample = 0;

	/* FxBus_init */
	t->dry_wet = 0;
	t->fx_type = LXR_FX_OFF;
	t->delay[0].d.delayType = DELAY_NORMAL;
	t->delay[1].d.delayType = DELAY_PINGPONG;
	Distortion_init(&t->dist);
	Ringmod_init(&t->ring);
	Compressor_init(&t->comp);
	Delay_init(&t->delay[0].d);
	Delay_init(&t->delay[1].d);
	t->fx_prev = LXR_FX_OFF;
	t->fx_fade = 0;
}

void lxr_drum_set(lxr_drum_t *t, int param, float v)
{
	if (param < 0 || param >= LXR_PARAM_COUNT || !isfinite(v)) return;
	g_cur = t;
	v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
	t->param[param] = v;
	t->param_cc[param] = -1;
	apply_float(t, param, v);
}

void lxr_drum_set_index(lxr_drum_t *t, int param, int index)
{
	if (param < 0 || param >= LXR_PARAM_COUNT || index < 0 || index > 127) return;
	g_cur = t;
	t->param[param] = (float)index / 127.f;
	t->param_cc[param] = (int16_t)index;
	apply(t, param, (uint8_t)index, (float)index / 127.f, 0);
}

void lxr_drum_set_lfo_target(lxr_drum_t *t, int index)
{
	if (index < 0 || index >= lxr_lfo_target_count || index == t->lfo_target) return;
	g_cur = t;
	lfo_reset_target(t);            /* modNode_setDestination: the old target goes home */
	t->lfo_target = index;
}

void lxr_drum_set_fx_param(lxr_drum_t *t, int type, int idx, float v)
{
	if (type <= LXR_FX_OFF || type >= LXR_FX_TYPES || idx < 0 || idx > 3 || !isfinite(v)) return;
	g_cur = t;
	v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
	t->fx_par[type][idx] = v;
	if (type == t->fx_type) { t->param[LXR_FX_P1 + idx] = v; t->param_cc[LXR_FX_P1 + idx] = -1; }
	if (type == t->fx_type || (t->fx_fade > 0 && type == t->fx_prev)) fx_update(t, type);
}

/* CC2_FX_TYPE: select, then FxBus_updateParameter. The outgoing FX keeps
 * running under a 10 ms crossfade. A delay coming back from being switched
 * off starts from silence, as it does at power-on: the firmware never clears
 * its delay memory, so whatever was in it would replay. */
void lxr_drum_set_fx_type(lxr_drum_t *t, int type)
{
	if (type < 0 || type >= LXR_FX_TYPES || type == t->fx_type) return;
	g_cur = t;
	if (type == LXR_FX_DELAY || type == LXR_FX_PINGPONG) {
		lxr_delay_unit_t *u = &t->delay[type == LXR_FX_PINGPONG];
		if (!(t->fx_fade > 0 && t->fx_prev == type)) memset(u->ram, 0, sizeof(u->ram));
	}
	t->fx_prev = t->fx_type;
	t->fx_type = type;
	t->fx_fade = LXR_FX_FADE_FRAMES;
	for (int i = 0; i < 4; ++i) { t->param[LXR_FX_P1 + i] = t->fx_par[type][i]; t->param_cc[LXR_FX_P1 + i] = -1; }
	fx_update(t, type);
}

void lxr_drum_set_dry_wet(lxr_drum_t *t, float v)
{
	if (!isfinite(v)) return;
	t->dry_wet = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);    /* CC2_FX_AMOUNT: value.itg/100.f */
}

void lxr_drum_set_dist_type(lxr_drum_t *t, int type)
{
	if (type >= 0 && type <= DISTORTION_RAT) t->dist.distortionType = (uint8_t)type;
}

void lxr_drum_set_ratio(lxr_drum_t *t, int ratio)
{
	if (ratio >= 0 && ratio <= 5) Compressor_setRatio(&t->comp, (uint8_t)ratio);
}

void lxr_drum_set_ring_wave(lxr_drum_t *t, int wave)
{
	g_cur = t;
	if (wave >= 0 && wave < LXR_NUM_WAVES) {
		Ringmod_setWave(&t->ring, (uint8_t)wave);
		osc_setFreq(&t->ring.osc);   /* a wavetable osc needs its table index */
	}
}

/* lfo_recalcSync for this voice, on a tempo change. */
void lxr_drum_set_bpm(lxr_drum_t *t, float bpm)
{
	if (!(bpm > 0.f) || bpm == t->bpm) return;
	g_cur = t;
	t->bpm = bpm;
	Lfo* lfo = &t->v.lfo;
	lfo->phaseInc = lfo_calcPhaseInc(lfo->freq,lfo->sync);
}

/* DrumVoice.c Drum_trigger(0, 127, note). Changed: lfo_retrigger(0) is the
 * one LFO it could reach (the firmware checks all six against voice 1); the
 * velocity modulator update is gone with velocity; Drum_updatePan inlined. */
void lxr_drum_trigger(lxr_drum_t *t, uint8_t note)
{
	g_cur = t;
	DrumVoice *v = &t->v;
	const uint8_t vol = 127;

	if(v->lfo.retrigger == 0+1)
	{
		v->lfo.phase = v->lfo.phaseOffset;
	}

	//only reset phase if envelope is closed
	//if((v->ampFilterInput<=0.01f) || (v->transGen.waveform==1))
	{
		float offset = 1;
		if(v->transGen.waveform==1) //offset mode
		{
			offset -= v->transGen.volume;
		}
		if(v->osc.waveform == SINE)
			v->osc.phase = 1024 + ( (0x3ff<<20) - 1024)*offset;//v->osc.startPhase ;
		else if(v->osc.waveform > SINE && v->osc.waveform <= REC)
			v->osc.phase = (0xff<<22)*offset;
		else
			v->osc.phase = 0;

		//reset mod osc phase
		if(v->modOsc.waveform == SINE)
			v->modOsc.phase = 1024 + ( (0x3ff<<20) - 1024);//v->osc.startPhase ;
		else if(v->modOsc.waveform > SINE && v->modOsc.waveform <= REC)
			v->modOsc.phase = (0xff<<22);
		else
			v->modOsc.phase = 0;

	}

	osc_setBaseNote(&v->osc,note);
	osc_setBaseNote(&v->modOsc,note);


	DecayEg_trigger(&v->oscPitchEg);
	slopeEg2_trigger(&v->oscVolEg);
	v->velo = vol/127.f;

	transient_trigger(&v->transGen);

	SnapEg_trigger(&v->snapEg);

	//reset filter coeffs to prevent wrong transient
	SVF_reset(&v->filter);

	//update pan value (Drum_updatePan)
	v->triggeredPanL = sqrtf((127-v->pan)/127.f);
	v->triggeredPanR = sqrtf(v->pan/127.f);
}

/* DrumVoice.c calcDrumVoiceAsync(0), `voiceArray[voiceNr]` -> `v`. */
static void voice_calcAsync(DrumVoice *v)
{
	//add modulation eg to osc freq (1 = no change. a+eg = original freq + modulation
	const float egPitchVal = DecayEg_calc(&v->oscPitchEg);
	const float pitchEgValue = egPitchVal*v->egPitchModAmount;
	v->osc.pitchMod = 1+pitchEgValue;

	//calc snap EG if transient sample 0 is activated
	if(v->transGen.waveform == 0)
	{
		const float snapVal = SnapEg_calc(&v->snapEg, v->transGen.pitch);
		v->osc.pitchMod += snapVal*v->transGen.volume;
	}

	// fm amount with pitch eg
	v->osc.fmMod = v->fmModAmount * egPitchVal;

	//calc the osc + noise vol eg
#if (AMP_EG_SYNC==0)

	//check if in attack phase
	if( (v->oscVolEg.attack == 1 ) && ((v->oscVolEg.state == EG_A) || (v->oscVolEg.state == EG_REPEAT)) )
	{
			//if attack is set to 0 -> no interpolation
		v->ampFilterInput = slopeEg2_calc(&v->oscVolEg);
		v->lastGain = v->ampFilterInput;
	}
	else
	{
		v->lastGain = v->ampFilterInput;
		v->ampFilterInput = slopeEg2_calc(&v->oscVolEg);
	}
#endif

	//update osc phaseInc
	osc_setFreq(&v->osc);
	osc_setFreq(&v->modOsc);
}

/* DrumVoice.c calcDrumVoiceSyncBlock(0, ...), `voiceArray[voiceNr]` -> `v`;
 * adc_getFaderVolume(0) is 1 (Move has no voice faders). */
static void voice_calcSyncBlock(DrumVoice *v, int16_t* buf, const uint8_t size)
{
	int16_t modBuf[size];

	//calc next mod osc sampleBlock - int16_t processing
	calcNextOscSampleBlock(&v->modOsc,modBuf,size,v->fmModAmount, v->pwm);

	if(v->mixOscs)
	{
		//calc main osc buffer - int16_t processing
		calcNextOscSampleBlock(&v->osc,buf,size, (1.f-v->fmModAmount), v->pwm);
		//add mod buffer to main osc buffer - int16_t processing
		bufferTool_addBuffersSaturating(buf,modBuf,size);
	}
	else
	{
		calcNextOscSampleFmBlock(&v->osc,modBuf,buf,size,1.0f, v->pwm);
	}

	//calc transient sample - int16_t processing
	transient_calcBlock(&v->transGen,modBuf,size);

	//Mix with transient buffer - int16_t processing
	bufferTool_addBuffersSaturating(buf,modBuf,size);

	//calc filter block - float processing
	SVF_calcBlockZDF(&v->filter,v->filterType,buf,size);

	//attentuate main OSCs by amp EG
	bufferTool_addGainInterpolated(buf,v->ampFilterInput, v->lastGain, size);

	//MIDI velocity - float processing
	if(v->volumeMod)
	{
		bufferTool_addGain(buf,v->velo,size);
	}
	//distortion - float processing
	calcDistBlockFiltered(&v->distortion,buf,size);

	for(int i=0; i< size; i++)
	{
		float mixVol 	= calcOnePole( &v->MixerPageFilter[0], v->vol);
		v->combinedVolume = mixVol * calcOnePole( &v->FaderVolumeFilter, 1.f);
		buf[i] *= v->combinedVolume;
	}
}

/* mixer.c mixer_calcNextSampleBlock, voice 0's statements, routed to the FX
 * bus (mixer_audioRouting[0] = MIXER_ROUTING_FX_BUS), then FxBus_process.
 * Added: the crossfade of an FX change. */
void lxr_drum_render(lxr_drum_t *t, int16_t *output)
{
	g_cur = t;
	DrumVoice *v = &t->v;

	lfo_reset_target(t);                         /* modNode_resetTargets */
	lfo_dispatch(t);                             /* lfo_dispatchNextValue */
	SVF_recalcFreq(&v->filter);
	voice_calcAsync(v);

	int16_t sampleData[OUTPUT_DMA_SIZE];
	int16_t fxBusData[OUTPUT_DMA_SIZE*2];
	memset(output, 0, OUTPUT_DMA_SIZE * 2 * sizeof(int16_t));
	memset(fxBusData, 0, sizeof(fxBusData));

	voice_calcSyncBlock(v, sampleData, OUTPUT_DMA_SIZE);

	/* mixer_decimateBlock(0, sampleData); mixer_decimation_rate[6] = 1 */
	{
		uint8_t i;
		float phaseInc = v->decimationRate*1.f;
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			t->dec_cnt += phaseInc;
			if(t->dec_cnt >= 1.f)
			{
				t->dec_cnt -= 1.f;
				t->dec_sample = sampleData[i];

			}
			sampleData[i] = t->dec_sample;
		}
	}

	/* mixer_addDataToOutput(MIXER_ROUTING_FX_BUS, triggeredPanL, triggeredPanR, ...) */
	{
		int16_t *fxBuffer = fxBusData;
		const float panL = v->triggeredPanL, panR = v->triggeredPanR;
		uint8_t i;
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			int32_t y;
			int32_t x;
			y = *((int32_t*)fxBuffer);
			x = __PKHBT( (int16_t)(sampleData[i] * panL), (int16_t)(sampleData[i] * panR), 16);

			*((int32_t*)fxBuffer) = __QADD16(x, y);
			fxBuffer += 2;
		}
	}

	if (t->fx_fade <= 0) {
		fx_process(t, t->fx_type, fxBusData, output, OUTPUT_DMA_SIZE);
		return;
	}
	int16_t was[OUTPUT_DMA_SIZE * 2];
	memset(was, 0, sizeof(was));
	fx_process(t, t->fx_prev, fxBusData, was, OUTPUT_DMA_SIZE);
	fx_process(t, t->fx_type, fxBusData, output, OUTPUT_DMA_SIZE);
	for (int i = 0; i < OUTPUT_DMA_SIZE; ++i) {
		int left = t->fx_fade - i;
		float g = left <= 0 ? 1.f : 1.f - (float)left / (float)LXR_FX_FADE_FRAMES;
		for (int c = 0; c < 2; ++c) {
			float s = (float)was[i * 2 + c] + ((float)output[i * 2 + c] - (float)was[i * 2 + c]) * g;
			output[i * 2 + c] = (int16_t)lrintf(s);
		}
	}
	t->fx_fade -= OUTPUT_DMA_SIZE;
	if (t->fx_fade < 0) t->fx_fade = 0;
}
