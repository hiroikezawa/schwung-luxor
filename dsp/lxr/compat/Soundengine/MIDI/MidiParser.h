/* LUXOR LXR port: the one MidiParser function the oscillator calls; its
 * body is copied verbatim into dsp/lxr/lxr_drum.c. GPL-3.0-only. */
#ifndef MIDIPARSER_H_
#define MIDIPARSER_H_
#include "stm32f7xx_hal.h"
float midiParser_calcDetune(uint8_t value);
#endif
