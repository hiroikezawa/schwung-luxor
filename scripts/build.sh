#!/usr/bin/env bash
# Build the LUXOR module.
#   scripts/build.sh native   desktop dsp.so for the tests (build/native)
#   scripts/build.sh arm64    Move dsp.so + install tarball (dist/)
set -euo pipefail
ID=luxor
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
# Schwung's plugin API header ships in third_party/schwung (MIT, see its
# LICENSE), unmodified.
API="$MODULE/third_party/schwung"
case "${1:-arm64}" in
  native) CC="${CC:-gcc}"; OUT="$MODULE/build/native" ;;
  arm64) CC="${CROSS_PREFIX:-aarch64-linux-gnu-}gcc"; OUT="$MODULE/dist/$ID" ;;
  *) echo "Usage: $0 [native|arm64]" >&2; exit 2 ;;
esac
GEN="$MODULE/build/generated"
OBJ="$MODULE/build/obj-${1:-arm64}"
rm -rf "$OBJ"
mkdir -p "$OUT" "$GEN" "$OBJ"
python3 "$MODULE/scripts/metadata.py" "$GEN/bk_metadata.h"
# The RANDOM pad's source MIDIs (Phrase Gen's), compiled in: nothing is read
# on the device.
python3 "$MODULE/scripts/embed_patterns.py" "$MODULE/patterns" "$GEN/pg_patterns_data.h" >/dev/null

# The LXR sound engine (GPL-3.0-only): the firmware's DSP files compiled
# UNMODIFIED from third_party/lxr (see dsp/lxr/README.md for the one patched
# file and why), against dsp/lxr/compat, which stands in for the STM32 HAL.
# lxr_sources.txt is the single list, shared with tests/lxr_ref/run.sh.
# -isystem for the vendored headers: searched AFTER compat (which must win),
# and their own warnings do not fail our -Werror files.
LXR_INC=(-I"$MODULE/dsp/lxr/compat" -I"$MODULE/dsp/lxr" -isystem "$MODULE/third_party/lxr/Core/Inc")
LXR_SRC=()
while read -r f; do [[ -n "$f" && "$f" != \#* ]] && LXR_SRC+=("$MODULE/$f"); done < "$MODULE/dsp/lxr/lxr_sources.txt"
# Floating point: no contraction into FMA (-ffp-contract=off), so the desktop
# test build, the reference renderer and the Move build compute the same
# samples; the firmware type-puns int16 pairs into int32 (SIMD on the M7),
# hence -fno-strict-aliasing. -fno-math-errno keeps sqrtf/fabsf inline on ARM
# (newer Ubuntu otherwise binds sqrtf@GLIBC_2.43, which Move's glibc 2.34
# does not have). No -ffast-math: the finite() guards must stay real.
COMMON=(-std=gnu11 -O2 -fPIC -fno-math-errno -ffp-contract=off -fno-strict-aliasing -fvisibility=hidden)
for f in "${LXR_SRC[@]}"; do
  # Vendored code keeps its own warnings (unused parameters, sign compares).
  "$CC" "${COMMON[@]}" -w "${LXR_INC[@]}" -c "$f" -o "$OBJ/lxr_$(basename "${f%.c}")_$(echo "$f" | cksum | cut -d' ' -f1).o"
done
"$CC" "${COMMON[@]}" -Wall -Wextra -Werror "${LXR_INC[@]}" -c "$MODULE/dsp/lxr/lxr_drum.c" -o "$OBJ/lxr_drum.o"
for f in "$MODULE"/dsp/*.c "$MODULE"/dsp/phrase/*.c; do
  "$CC" "${COMMON[@]}" -Wall -Wextra -Werror -I"$API" -I"$GEN" -I"$MODULE/dsp" "${LXR_INC[@]}" \
    -c "$f" -o "$OBJ/$(basename "${f%.c}").o"
done
"$CC" -shared -Wl,-z,defs "$OBJ"/*.o -o "$OUT/dsp.so" -lm
cp "$MODULE/module.json" "$OUT/"
if [[ "${1:-arm64}" == arm64 ]]; then
  python3 "$MODULE/scripts/check_abi.py" "$OUT/dsp.so"
  for f in ui_chain.js ui_core.mjs help.json README.md; do
    [[ -f "$MODULE/$f" ]] && cp "$MODULE/$f" "$OUT/"
  done
  cp "$MODULE/LICENSE" "$MODULE/THIRD_PARTY_LICENSES.md" "$OUT/"
  # LICENSE is the GPL-3.0 text; the LXR firmware's additional terms ride along.
  mkdir -p "$OUT/licenses"
  cp "$MODULE/third_party/lxr/NOTICE.md" "$OUT/licenses/LXR-NOTICE.md"
  tar -czf "$MODULE/dist/$ID-module.tar.gz" -C "$MODULE/dist" "$ID"
fi
file "$OUT/dsp.so"
