/* Reference build: Core/Inc/Soundengine/globals.h without the HAL. */
#ifndef GLOBALS_H_
#define GLOBALS_H_
#include "stm32f7xx.h"
#include "Soundengine/config.h"
extern volatile uint32_t systick_ticks;
extern volatile uint8_t bCurrentSampleValid;
#define SAMPLE_VALID  0xff
#define FILTER_SHAPER -0.9f
#endif
