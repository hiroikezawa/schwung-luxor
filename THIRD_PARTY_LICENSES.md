# Third-party code in LUXOR

LUXOR as a whole -- this repository, the built `dsp.so` and
`luxor-module.tar.gz` -- is licensed under the **GNU General Public License
version 3 only** (`LICENSE`), because its sound engine is the LXR firmware's
below, which is GPL-3.0-only. LUXOR's own code is
Copyright (C) 2026 Hiro Ikezawa, under the same licence. The one exception is Schwung's plugin API header
(MIT, below), which keeps its own licence; MIT is GPL-compatible.
`dsp.so` needs only `libc` and `libm` (`scripts/check_abi.py` checks the build).

## LXR firmware sound engine (Erica Synths) — GPL-3.0-only

Source: https://github.com/erica-synths/lxr-desktop-app, commit in
`third_party/lxr/UPSTREAM_COMMIT`. Copyright (C) 2012-2026 Erica Synths,
licensed under the GNU GPL version 3 only with the additional permission and
additional terms of `third_party/lxr/NOTICE.md` (GPL-3.0 section 7). The full
licence is `third_party/lxr/LICENSE` (in the package: `licenses/GPL-3.0.txt`,
`licenses/LXR-NOTICE.md`). Every vendored file keeps its copyright header.

- `third_party/lxr/` — the firmware's own files, **unmodified** (CRLF line
  ends as upstream). Compiled into `dsp.so`: the list in
  `dsp/lxr/lxr_sources.txt` (oscillators and wavetables, the ZDF state-variable
  filter, the envelopes, the transient generator and its samples, the snap EG,
  voice distortion, dither, one-pole filter, and the Ringmod, Compressor and
  Delay FX). The rest of what is there (`DrumVoice.c`, `lfo.c`,
  `modulationNode.c`, `MidiParser.c`, `ParameterArray.c`, `FxBus.c`,
  `mixer.c`, the headers they include) is the original of the adapted code
  below and is compiled only by the reference renderer in `tests/lxr_ref/`.
- `dsp/lxr/lxr_drum.c`, `dsp/lxr/lxr_drum.h`, `dsp/lxr/lxr_params.h` — the DRUM
  voice, LFO, modulation node, DRUM 1 / FX parameter handling, voice mixer
  slice and FX bus, **modified** to run per instance instead of from the
  firmware's global arrays (every change is described at the function).
- `dsp/lxr/patched/Oscillator.c`, `dsp/lxr/patched/Distortion.c` — the
  firmware files with one out-of-bounds read each fixed, **modified** (the
  change is described in each file's header).
- `dsp/lxr/compat/` — stand-ins for the STM32 HAL / CMSIS and for the firmware
  headers the DSP includes; `Soundengine/config.h`, `globals.h`,
  `ParameterArray.h` and `FxRamBuffer.h` there carry definitions from the
  firmware's headers and are GPL-3.0-only.
- `tests/lxr_ref/` — the reference renderer and its scenes, GPL-3.0-only.

Trademarks: "Erica Synths", "LXR" and "LXR-02" belong to Erica Synths and are
not licensed by the GPL (NOTICE.md 7(e)). LUXOR is an independent,
unofficial program, not made, approved or supported by Erica Synths; the
names appear only to say truthfully where the ported source comes from. The
LXR hardware design is not part of that source and nothing of it is used.

## The phrase engine and source MIDI files — the author's own work

`dsp/phrase/` (the MIDI loader, analyser, generator, scale quantizer and
sequencer) and `patterns/` (the six source MIDI files) are the author's own
work (Hiro Ikezawa), under this repository's licence (GPL-3.0-only). One change in
`pg_sequencer.c`, marked in the source, makes a queued pattern swap on the
longer of the old and new lengths. LUXOR uses only the loader, analyser
and generator, for the RANDOM pad (`dsp/bk_phrase.c`); the sequencer is
compiled but unused.

## Schwung — MIT

`third_party/schwung/host/plugin_api_v1.h` is Schwung's plugin API header
(Copyright 2025-2026 Charles Vestal, MIT), vendored unmodified. Its licence (also in
`third_party/schwung/LICENSE`):

```
MIT License

Copyright (c) 2025-2026 Charles Vestal

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

