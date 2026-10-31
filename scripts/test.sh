#!/usr/bin/env bash
# Native build + every test: engine (ASan/UBSan), real dlopen smoke, UI against
# Schwung's real page engine, then the Move build (check_abi.py rejects a libm
# symbol newer than Move's glibc 2.34).
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
bash "$MODULE/scripts/build.sh" native
OUT="$MODULE/build/native"
# Engine tests under ASan + UBSan. The vendored LXR files get ASan only: UBSan
# flags patterns the firmware relies on and that are well defined on both
# ARMs it runs on (a left shift of a negative sample, an interpolation read
# that steps from one row of a wavetable into the next), and those reports
# would stop the run before our own code is reached. The reference test
# (tests/lxr_ref) runs both builds fully sanitized with recovery on.
LXR_INC=(-I"$MODULE/dsp/lxr/compat" -I"$MODULE/dsp/lxr" -isystem "$MODULE/third_party/lxr/Core/Inc")
SAN_FLAGS=(-std=gnu11 -D_DEFAULT_SOURCE -O1 -g -fno-math-errno -ffp-contract=off -fno-strict-aliasing
           -fno-omit-frame-pointer)
EOBJ="$OUT/engine-obj"; rm -rf "$EOBJ"; mkdir -p "$EOBJ"
while read -r f; do
  [[ -n "$f" && "$f" != \#* ]] || continue
  "${CC:-gcc}" "${SAN_FLAGS[@]}" -fsanitize=address -w "${LXR_INC[@]}" -c "$MODULE/$f" \
    -o "$EOBJ/$(basename "${f%.c}")_$(echo "$f" | cksum | cut -d' ' -f1).o"
done < "$MODULE/dsp/lxr/lxr_sources.txt"
"${CC:-gcc}" "${SAN_FLAGS[@]}" -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
  -I"$MODULE/dsp" "${LXR_INC[@]}" "$MODULE/tests/test_engine.c" "$MODULE/dsp/bk_engine.c" \
  "$MODULE/dsp/bk_clock.c" "$MODULE/dsp/bk_motion.c" "$MODULE/dsp/lxr/lxr_drum.c" "$EOBJ"/*.o \
  -o "$OUT/test-engine" -fsanitize=address,undefined -lm
"$OUT/test-engine" --wav "$OUT/demo.wav"
python3 "$MODULE/tests/smoke.py" "$OUT/dsp.so"
# The port against the LXR firmware's own code, sample for sample.
bash "$MODULE/tests/lxr_ref/run.sh" "$MODULE/build/lxr_ref"
SANITIZE=1 UBSAN_OPTIONS=print_stacktrace=0:report_error_type=1 bash "$MODULE/tests/lxr_ref/run.sh" "$MODULE/build/lxr_ref_asan" 2>/dev/null | tail -1
# The UI test drives Schwung's own page engine and draw harness: it needs a
# Schwung checkout (SCHWUNG_ROOT, else ../schwung beside this repository).
SCHWUNG="${SCHWUNG_ROOT:-$(cd "$MODULE/.." && pwd)/schwung}"
if [[ ! -f "$SCHWUNG/src/shared/param_pages/page_controller.mjs" ]]; then
  echo "SKIP: UI test needs a Schwung checkout (set SCHWUNG_ROOT)" >&2
elif command -v node >/dev/null 2>&1; then
  SCHWUNG_ROOT="$SCHWUNG" node "$MODULE/tests/test_ui.mjs"
elif command -v node.exe >/dev/null 2>&1 && command -v wslpath >/dev/null 2>&1; then
  node.exe "$(wslpath -w "$MODULE/tests/test_ui.mjs")"
else
  echo "Node.js is required for the UI test" >&2; exit 1
fi
if command -v "${CROSS_PREFIX:-aarch64-linux-gnu-}gcc" >/dev/null 2>&1; then
  bash "$MODULE/scripts/build.sh" arm64
else
  echo "SKIP: no aarch64 cross compiler; ABI not checked" >&2
fi
