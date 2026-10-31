/* LUXOR LXR port: what the DSP files take from the LXR sequencer header --
 * the note a sound is tuned at, and the tempo (Move's, see lxr_drum.c).
 * GPL-3.0-only. */
#ifndef SEQUENCER_H_
#define SEQUENCER_H_
#include "stm32f7xx_hal.h"
#define SEQ_DEFAULT_NOTE 60
float seq_getBpm(void);
#endif
