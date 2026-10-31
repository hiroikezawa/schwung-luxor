/* LUXOR - host stand-in for the STM32 HAL / CMSIS header the LXR sound
 * engine sources include. Part of the LXR port; GPL-3.0-only (see
 * dsp/lxr/README.md).
 *
 * The LXR DSP files only need integer types, <math.h> and a handful of
 * Cortex-M DSP intrinsics. Each intrinsic below is written out in portable C
 * with the exact semantics of the ARMv7E-M instruction it names (ARM DDI 0403,
 * the CMSIS-Core reference), so the vendored files compile UNMODIFIED on
 * AArch64 and on the desktop test build and compute the same integers. */
#ifndef BK_LXR_COMPAT_STM32F7XX_HAL_H
#define BK_LXR_COMPAT_STM32F7XX_HAL_H

#include <stddef.h>
#include <stdint.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* IEEE binary16, which the FX delay line stores. ARM GCC has __fp16; the
 * desktop compilers spell the same type _Float16 (identical rounding). */
#if !defined(__ARM_FP16_FORMAT_IEEE) && !defined(__aarch64__)
#define __fp16 _Float16
#endif

/* The FX Distortion's foldback() calls fmod(). glibc 2.38 added a new fmod
 * version, which a current cross toolchain binds and Move's glibc 2.34 does
 * not have (the module would not load); the GLIBC_2.17 one computes the
 * same result (the new one only changed errno behaviour). */
#if defined(__aarch64__) && defined(__GLIBC__)
__asm__(".symver fmod,fmod@GLIBC_2.17");
#endif

static inline int32_t bk_lxr_sat16(int32_t x) {
    return x > 32767 ? 32767 : (x < -32768 ? -32768 : x);
}

/* QADD16 / QSUB16: two signed halfword lanes, each saturated to int16. */
static inline uint32_t __QADD16(uint32_t a, uint32_t b) {
    int32_t lo = bk_lxr_sat16((int32_t)(int16_t)(a & 0xFFFFu) + (int32_t)(int16_t)(b & 0xFFFFu));
    int32_t hi = bk_lxr_sat16((int32_t)(int16_t)(a >> 16) + (int32_t)(int16_t)(b >> 16));
    return ((uint32_t)lo & 0xFFFFu) | ((uint32_t)hi << 16);
}
static inline uint32_t __QSUB16(uint32_t a, uint32_t b) {
    int32_t lo = bk_lxr_sat16((int32_t)(int16_t)(a & 0xFFFFu) - (int32_t)(int16_t)(b & 0xFFFFu));
    int32_t hi = bk_lxr_sat16((int32_t)(int16_t)(a >> 16) - (int32_t)(int16_t)(b >> 16));
    return ((uint32_t)lo & 0xFFFFu) | ((uint32_t)hi << 16);
}
/* SSAT #n: saturate a signed value to an n-bit signed range. */
static inline int32_t __SSAT(int32_t val, uint32_t sat) {
    const int32_t max = (int32_t)((1u << (sat - 1u)) - 1u), min = -max - 1;
    return val > max ? max : (val < min ? min : val);
}
/* PKHBT: bottom halfword of a, (b << n) into the top halfword. */
#define __PKHBT(ARG1, ARG2, ARG3) \
    ((((uint32_t)(ARG1)) & 0x0000FFFFu) | ((((uint32_t)(ARG2)) << (ARG3)) & 0xFFFF0000u))
/* CLZ: 32 for a zero input, which Oscillator.c relies on (a frequency below
 * 440 Hz gives fast_log2(0) = 31 - 32). */
static inline uint8_t __CLZ(uint32_t x) {
    return x ? (uint8_t)__builtin_clz(x) : 32u;
}

/* calcNoise() (compiled, never called by the drum voice) reads the overflow
 * flag of the last add. Nothing here can see it; it reads as clear. */
typedef union {
    struct { uint32_t _reserved0:27; uint32_t Q:1; uint32_t V:1; uint32_t C:1; uint32_t Z:1; uint32_t N:1; } b;
    uint32_t w;
} APSR_Type;
static inline uint32_t __get_APSR(void) { return 0u; }

/* random.h declares the firmware's hardware RNG handle; GetRngValue() is
 * implemented per instance in lxr_drum.c instead. */
typedef struct { int unused; } RNG_HandleTypeDef;

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

#endif
