#!/usr/bin/env bash
# Render every scene of scenarios.h through the LXR firmware's own code
# (ref_main) and through the port (port_main), and compare them sample for
# sample. Usage: tests/lxr_ref/run.sh [outdir]
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$MODULE/build/lxr_ref}"
mkdir -p "$OUT"
CC="${CC:-gcc}"
T="$MODULE/third_party/lxr/Core"
S="$T/Src/Soundengine/DSPAudio"
DSP=""
while read -r f; do [[ -n "$f" && "$f" != \#* ]] && DSP="$DSP $MODULE/$f"; done < "$MODULE/dsp/lxr/lxr_sources.txt"
# Same floating-point contract as the module build (scripts/build.sh).
FLAGS="-std=gnu11 -O2 -fno-strict-aliasing -ffp-contract=off -fno-math-errno -w"
# SANITIZE=1: the same comparison under ASan + UBSan (both builds).
[[ "${SANITIZE:-0}" == 1 ]] && FLAGS="$FLAGS -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer"
# The reference: the firmware's parser, voice, LFO, modulation, FX bus.
# delay.h (a cycle-counter busy-wait) and main.h (HAL handles) are kept out by
# their include guards; tests/lxr_ref/include stands in for the HAL.
"$CC" $FLAGS -DDWT_STM32_DELAY_H -D__MAIN_H -I"$MODULE/tests/lxr_ref/include" -I"$T/Inc" \
  -I"$MODULE/dsp/lxr" -I"$MODULE/tests/lxr_ref" \
  "$MODULE/tests/lxr_ref/ref_main.c" "$S/DrumVoice.c" "$S/lfo.c" "$S/modulationNode.c" \
  "$S/squareRootLut.c" "$S/FX/FxRamBuffer.c" "$T/Src/Soundengine/MIDI/MidiParser.c" \
  "$T/Src/Soundengine/MIDI/ParameterArray.c" "$T/Src/Soundengine/FxBus.c" $DSP \
  -o "$OUT/ref_main" -lm
"$CC" $FLAGS -I"$MODULE/dsp/lxr/compat" -I"$T/Inc" -I"$MODULE/dsp/lxr" -I"$MODULE/tests/lxr_ref" \
  "$MODULE/tests/lxr_ref/port_main.c" "$MODULE/dsp/lxr/lxr_drum.c" $DSP -o "$OUT/port_main" -lm
python3 "$MODULE/tests/lxr_ref/compare.py" "$OUT"
