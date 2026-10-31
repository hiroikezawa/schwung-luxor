/* LUXOR LXR port: modulationNode.h only needs the value union of the
 * firmware's ParameterArray.h (Copyright (C) 2012-2026 Erica Synths).
 * GPL-3.0-only. */
#ifndef PARAMETERARRAY_H_
#define PARAMETERARRAY_H_
#include "stm32f7xx_hal.h"
typedef union
{
	float 	 flt;
	uint32_t itg;
} ptrValue;
#endif
