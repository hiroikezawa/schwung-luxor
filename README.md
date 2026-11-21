# LUXOR

**English** | [日本語](README.ja.md)

(Module ID: `luxor`)

A Sound Generator for Schwung / Ableton Move. LUXOR is a percussion voice that **plays
by itself on every 1/16 note, in time with Move's transport** — no clips or MIDI notes
needed.

The sound engine is the **DRUM voice** (an FM drum voice) and the **FX (Drive / Ring /
Comp / Delay / Ping-Pong)** from the open-source firmware (GPL-3.0) of Erica Synths'
**LXR-02** drum machine, ported without changing the calculations.
(LUXOR is unofficial software, not affiliated with Erica Synths. See the note at the end.)

It aims for the crunchy, grinding sound you get from a longish decay hammered at 1/16,
where each hit is still ringing when the next one stacks on top of it.

- **Record knob motion**: turn on the REC pad (top right) and turn a knob while touching
  it. The motion is recorded into the loop and played back every pass. Only the time
  you are touching the knob is recorded.
- **Choose the steps on the pads**: the bottom two pad rows switch the 16 steps on and off.
- **Generate a melody**: press the RANDOM pad next to REC and a random melody is written
  in as a pitch per step.
- **Store 8 presets**: the second pad row from the top saves the sound, the knob motion,
  the steps and the scale together, and recalls them with one tap.
- **Pitch stays in the scale**: PITCH (knob 8) only picks notes of the selected scale
  (C Phrygian by default).
- **Nearly every parameter of the LXR-02 DRUM voice is available** (oscillators, amp EG,
  pitch / FM envelope, FM, transient, filter, LFO, drive, sample rate).
- **One LXR-02 FX can be applied**. Each FX keeps its own settings when you switch types.

---

## Building

This repository builds on its own. The Schwung plugin API header (`plugin_api_v1.h`,
MIT) is bundled in `third_party/schwung/` and the source of the LXR-02 firmware the
engine comes from in `third_party/lxr/` (both are unmodified copies).

```bash
scripts/build.sh arm64
```

→ `dist/luxor-module.tar.gz` (AArch64 `dsp.so` for Move + UI + licences)

Requirements: `aarch64-linux-gnu-gcc`, `python3`. During the build `scripts/check_abi.py`
checks that the library is AArch64, needs glibc 2.34 or older, and exports
`move_plugin_init_v2`, and fails the build otherwise.

The full test suite (native ASan/UBSan engine tests, a dlopen smoke test of the real
`dsp.so`, a waveform comparison against the original LXR-02 firmware code, UI tests and
finally the arm64 build):

```bash
scripts/test.sh
```

The UI tests need Node.js and a checkout of Schwung itself (for its page engine and
drawing harness): `../schwung` next to this repository, or
`SCHWUNG_ROOT=/path/to/schwung`. If neither is found, only the UI tests are skipped
(on WSL the Windows `node.exe` works too).

## Installing on Move

Built packages are on the GitHub **Releases** page (`luxor-module.tar.gz`); you do not
need to build anything yourself.

Upload through Schwung Manager (`http://<Move's IP>:7700`):

```bash
# install a package downloaded from Releases
scripts/install.py http://move.local:7700 --package luxor-module.tar.gz
# install your own build (dist/)
scripts/install.py http://move.local:7700
```

(`--check` only tests the connection.) Then choose **LUXOR** as the Synth of a chain slot.
You can also upload `luxor-module.tar.gz` from Schwung Manager's Modules page
("Install custom").

**Restart Move after an update.** The old `dsp.so` stays loaded in the process, so
re-selecting the slot does not switch to the new version.

## Getting started

The basic flow is: put it in a slot → press Play → shape the sound with the knobs →
build a pattern on the pads → record knob motion with REC.

### 1. Make a sound

1. Open a slot on the Schwung screen (Shift+Vol+Track, or hold a track button) and
   choose **LUXOR** as the Synth.
2. Open the slot's Synth (LUXOR) to get the LUXOR screen.
3. Press **Play** on Move. LUXOR **plays by itself on every 1/16 note, in time with the
   tempo**. On Stop the sound fades out in about 25 ms (FX tails included) and nothing
   plays while stopped. MIDI notes do not play it (the Move track's clips can stay empty).

### 2. Shape the sound (main page)

| Knobs | | | |
|---|---|---|---|
| 1 VOLUME | 2 P.ENV | 3 FM AMT | 4 MOD DEC |
| 5 CUTOFF | 6 DRIVE | 7 DECAY | 8 PITCH |

These are good ones to start with:

- **DECAY** (knob 7): note length. The longer it is, the more each hit overlaps the next.
- **P.ENV** (knob 2) and **MOD DEC** (knob 4): how far the pitch drops from above at the
  moment of the hit, and how fast. Small values give kicks and toms; large values give
  a "pew" laser sound.
- **FM AMT** (knob 3): FM depth. Higher is more metallic and noisy. The FM depth also
  falls away at the MOD DEC rate on every hit, so use it together with knob 4.
- **CUTOFF** (knob 5): filter brightness (low-pass by default).
- **DRIVE** (knob 6): voice distortion. Higher is thicker and rougher.
- **PITCH** (knob 8): pitch. Each click moves to the next note of the scale and the
  note name appears on screen.

**Turn the jog wheel** to move to the other pages (see the page table below).

### 3. Build a pattern (pads)

While the screen is open the pads belong to LUXOR (they do not reach Move).

```
                              RANDOM  REC   ← right two pads of the top row
 P1  P2  P3  P4  P5  P6  P7  P8             ← second row from the top (presets)
  9  10  11  12  13  14  15  16             ← second row from the bottom
  1   2   3   4   5   6   7   8             ← bottom row
```

- The **bottom two rows** switch whether that step plays: on (bright) / off (dark).
  The step lit white is the one playing now.
- **RANDOM** writes a new melody into PITCH. Press it as often as you like until you
  get one you like. The range is centred on PITCH (knob 8). It works with REC off and
  while stopped. To undo, **hold Shift and touch knob 8** (clears the PITCH recording).
  See "RANDOM (melody generation)" below for details.
- **The second row from the top (P1–P8)** holds presets. **Hold Shift and tap** to save
  the current state to that pad; **tap** to recall it. See "Presets" below.

### 4. Record knob motion (REC)

1. Press **REC** (the top-right pad) to turn it on (solid red; dim when off).
2. With Move playing, **touch and turn** the knob you want to record. The motion is
   recorded into the loop only while you are touching it; letting go ends the take.
   Even without turning, touching overwrites with the current value.
3. A recorded knob plays its motion back on every pass from then on. The knob's own
   value does not change, and wherever nothing is recorded the knob's value plays.
4. To erase: **hold Shift and touch a knob** to clear that knob's recording.
   **Shift + touching knob 1 (VOLUME)** clears everything.
5. When you are done, press REC again to turn it off so a stray touch records nothing.

The seven main-page knobs 2–8 can be recorded (P.ENV, FM AMT, MOD DEC, CUTOFF, DRIVE,
DECAY, PITCH). VOLUME is not recorded, to avoid a sudden blast from an accidental take.
Knobs on the other pages (including the FX page) are not recorded for now.

### 5. Add an FX

Turn the jog wheel to the **FX** page.

1. Choose the FX with **knob 2 (FX TYPE)**: Off / Drive / Ring / Comp / Delay / PingPong.
2. Set the dry/wet balance with **knob 1 (MIX)** (0 % = dry only, 100 % = FX only).
3. **From knob 3 on** only the selected FX's parameters are laid out (see the "FX"
   table below). Knobs the FX does not use are not shown and do nothing.

Each FX type keeps its own settings, so switching FX TYPE away and back restores them.
Switching FX TYPE crossfades over 10 ms, so it does not click. Re-selecting Delay /
PingPong starts with an empty delay line.

### 6. When you are done

Close the screen with **Back**. The pads go back to Move with their colours restored.
The sound, FX, recordings and step on/off are saved with the set (the slot's autosave).
The REC pad is not saved and always starts off when the screen is reopened.

## Controls (reference)

The screen and knobs look and work like Schwung's standard knob screen (eight knobs per
page), with knob touch (for recording) and the pads added.

| Control | Action |
|---|---|
| Knobs 1–8 | Parameters of the current page (page table below) |
| Touch a knob (REC on, playing) | Records the motion of main-page knobs 2–8 |
| Shift + touch a knob | Clears that knob's recording; on VOLUME (knob 1) clears **everything** |
| Hold Shift while turning | Fine adjustment |
| Jog turn / click | Change page, select (same as Schwung's standard knob screen) |
| Back | Close the screen (pads go back to Move) |

Pages (based on the LXR-02's own menu pages, with FX right after Filter, and the
rarely touched TUNE, FINE and the loop length LENGTH gathered on the last page):

| Page | Knobs 1–8 |
|---|---|
| 1 Main | VOLUME, P.ENV, FM AMT, MOD DEC, CUTOFF, DRIVE, DECAY, PITCH |
| 2 Osc | WAVE, PWM, ATTACK, A.SLOPE, P.SLOPE |
| 3 FM | FM FREQ, MOD WAV, MODE, TRANS, TR VOL, TR FRQ |
| 4 Filter | RESO, F.TYPE, F.DRIVE, PAN, S.RATE |
| 5 FX | MIX, FX TYPE, (the selected FX's parameters) |
| 6 LFO | RATE, SYNC, AMOUNT, WAVE, RETRIG, PHASE, DEST |
| 7 Scale | ROOT, SCALE, LENGTH (number of steps), TUNE, FINE |

The top of the screen shows the loop position (e.g. `LUXOR 5/16`), `ARM` while REC is on,
`REC` while recording, and the note name while PITCH is touched (e.g. `PITCH Eb2`).
Knobs with a recording use Schwung's "modulated" display (the playing value moves).

### Voice parameters

These map directly onto the LXR-02 DRUM voice parameters (0–127 on the hardware;
TUNE, FM FREQ, FINE and PAN are shown with the same numbers as the hardware).

| Knob | What it does |
|---|---|
| VOLUME | Voice level. 0 % is complete silence |
| DECAY | Amp EG decay. Time until the sound dies away: about 0.12 s at 25 %, 0.3 s at 50 %, 0.5 s at 60 % (default), 1.2 s at 75 %, 4 s at 90 %. The curve gives the short end finer control |
| P.ENV | Pitch EG depth (MOD AMOUNT on the hardware). The pitch at the moment of the hit rises to about 3× (+19 semitones) at 25 %, about 9× at 50 %, and 33× at 100 % |
| FM AMT | FM depth (FM AMOUNT on the hardware). With MODE set to Mix, the balance of the two oscillators |
| MOD DEC | Pitch EG decay (PITCH DECAY on the hardware). The FM depth also falls with this EG |
| CUTOFF | Filter frequency. About 340 Hz at 25 %, 1 kHz at 50 %, 2.7 kHz at 75 %, 20 kHz at 100 % |
| DRIVE | Voice distortion (DRIVE on the hardware's MIX page) |
| PITCH | The pitch of the hit (scale notes only). C1 (32.7 Hz) to C5 (523 Hz); 29 notes in C Phrygian |
| TUNE | Transpose in semitones (COARSE on the hardware; 0 plays PITCH as is) |
| FINE | Fine tuning (about ±50 cents) |
| WAVE | Main oscillator waveform: Sine / Tri / Saw / Rect / Noise / PWM |
| PWM | Pulse width when WAVE is PWM |
| ATTACK | Amp EG attack. 0 % rises instantly |
| A.SLOPE | Amp EG curve |
| P.SLOPE | Pitch EG curve (see the note below) |
| FM FREQ | Pitch of the modulator (the second oscillator), in semitones (default +12) |
| MOD WAV | Modulator waveform (the same six as WAVE) |
| MODE | FM (the modulator frequency-modulates the main oscillator) / Mix (the two are mixed) |
| TRANS | Attack transient: Snap (a brief pitch jump) / Offset (a click from shifting the waveform's start) / Click / Click2 / Tick / Kick / Rim / Drop / Hat / Clap / Kick2 / Snare / Tom / Snap2 (short samples) |
| TR VOL / TR FRQ | Transient level / speed (pitch) |
| RESO | Filter resonance |
| F.TYPE | Filter type: LP / HP / BP / UnityBP / Notch / Peak / LP2 / Off |
| F.DRIVE | Distortion before the filter |
| PAN | Panning (-63 left to +64 right). The value at each hit is used |
| S.RATE | Sample rate (100 % is the clean sound, lower is more lo-fi; 0 % stops the sound) |
| RATE / SYNC | LFO speed / tempo sync (Off / 4/1 / 2/1 / 1/1 / 1/2 / 1/3 / 1/4 / 1/6 / 1/8 / 1/12 / 1/16 / 1/32, synced to Move's tempo) |
| AMOUNT | LFO depth (0 % has no effect) |
| WAVE (LFO) | Sine / Tri / SawUp / SawDown / Square / Random / ExpUp / ExpDown |
| RETRIG | When On, every hit restarts the LFO from PHASE |
| PHASE | LFO start position |
| DEST | LFO destination: TUNE / FINE / WAVE / PWM / ATTACK / DECAY / A.SLOPE / MOD DEC / P.SLOPE / P.ENV / FM AMT / FM FREQ / MOD WAV / MODE / TRANS / TR VOL / TR FRQ / CUTOFF / RESO / F.TYPE / F.DRIVE / VOLUME / PAN / S.RATE / DRIVE / FX P1–P4 (knobs 3–6 of the selected FX) |

- PITCH, TUNE, FM FREQ and PAN take their value at the moment of each 1/16 hit.
- Changing ROOT or SCALE moves PITCH, and any recorded PITCH motion, to the nearest note
  of the new scale.
- P.SLOPE's maximum corresponds to 126 on the hardware. At the hardware's 127 the pitch
  EG divides by zero and the oscillator stops, so that single step is left out.

### FX

| FX TYPE | Knobs 3 and up |
|---|---|
| Drive | TYPE (Tube / Fold / Clip), DRIVE, COLOR, TONE, VOLUME |
| Ring | WAVE (carrier waveform), FREQ |
| Comp | RATIO (1:1 to 8:1), ATTACK, DECAY (release), THRESH, GAIN |
| Delay | TIME, RANGE, TONE, FDBK |
| PingPong | TIME, RANGE, TONE, FDBK (a delay bouncing left and right) |

- With Drive's Clip, raising COLOR feeds the output back to the input, so it
  self-oscillates and keeps sounding (the same as the LXR-02 itself).
- Delay / PingPong at 100 % FDBK never keeps getting louder.
- The original firmware also contains reverb source code, but it is disabled in the
  official build (`USE_REVERB 0`), so LUXOR does not include it either.

### Pads

**While this screen is open, LUXOR takes over the pads.** Move's own instruments and
sequencer do not receive pad presses. Closing the screen gives the pads back to Move with
their colours restored (in Schwung a module can only take the pads while its screen is
shown).

```
top     .   .   .   .   .   .   RANDOM  REC      ← REC is the top-right pad
        P1  P2  P3  P4  P5  P6  P7      P8       ← presets 1–8
        9  10  11  12  13  14   15      16       ← triggers 9–16
bottom  1   2   3   4   5   6    7       8       ← triggers 1–8
```

| Pad | Action |
|---|---|
| **REC** (top right) | Knob-motion recording on/off. **On = solid red, off = dim** |
| **RANDOM** (left of REC) | Generates a new melody and writes it into PITCH, step by step |
| Second row from the top (8 pads) | Presets. Tap = recall, Shift + tap = save |
| Bottom two rows (16 pads) | Each step's hit on/off. On = bright, off = dark, the playing step = white |
| Everything else | Nothing |

- Hits are 1/16 notes locked to Move's transport, with the first step on the bar line.
- An off step starts no new hit; the previous note keeps decaying. Recorded knob motion
  keeps running through off steps.
- With LENGTH at 1/2 Bar the second row from the bottom (9–16) is dark and unused. At
  2 Bars / 4 Bars the same 16 steps repeat every bar.
- See the next section for the details of RANDOM.

### Presets

The eight pads of the second row from the top each store the whole current state.

- **Save**: hold Shift and tap a pad. The top of the screen shows e.g. `SAVED PRESET 3`.
  Saving to the same pad again overwrites it.
- **Recall**: tap a pad. It switches on the spot, playing or stopped. An empty pad shows
  `EMPTY 3 (SHIFT+PAD SAVES)` and changes nothing.
- **What is saved**: the values of every knob on every page, the recorded knob motion,
  the on/off of the 16 steps, and ROOT / SCALE / LENGTH. The REC pad's state is not saved.
- **Pad colours**: off = empty, dim = saved, white = the one last recalled (or saved).
- Presets are stored on Move in `/data/UserData/schwung/luxor/presets.json`. The same
  eight are available to LUXOR in every slot and survive switching sets.

### RANDOM (melody generation)

RANDOM **builds a slightly different melody each time from the MIDI files (source
phrases) in `patterns/` and writes it into PITCH**. Six source phrases are included.
You can replace them with your own MIDI, which changes the melodies RANDOM makes
(see "Replacing the source phrases" below).

**What happens when you press it**

1. One source phrase is picked at random (the one used last time is less likely).
2. Its sequence of notes is varied by "50 % mutation": roughly half of the notes change
   — moving to a neighbouring scale note, jumping an octave, repeating the previous
   note, and so on — and the rest stay as in the source. The result differs every time
   and never repeats the previous melody.
3. No rests are made. Rests in the source phrase are filled with notes too, so every step
   gets a pitch.
4. The melody is written into the loop's steps as a PITCH recording.
   - With LENGTH at 1/2 Bar an 8-step melody is made; at 1 Bar or longer a 16-step
     melody (two 8-step variations in a row), repeated at 2 / 4 Bars.
   - Any earlier PITCH recording is overwritten for the length of the loop.
   - Whether a step plays is set by the bottom two pad rows. RANDOM does not change the
     step on/off.

**Range and scale**

- Every written note is a note of the scale set on the Scale page (ROOT, SCALE). Like
  knob 8's value it is written as "the nth note of the scale", so it never drifts later.
- The range is **based on the note of knob 8 (PITCH), from a fifth below to an octave and
  a half above** (-7 to +17 semitones). Notes outside it are moved by octaves so that
  they spread across the whole range. The note names are kept and knob 8 sets the
  overall register (set knob 8 before pressing).
  When knob 8 is near either end of the pitch range, the range is narrowed rather than
  shifted (at C1 it is C1 to F2).

**Undo / try again**

- Press it again to write a different melody.
- **Shift + touch knob 8** clears the PITCH recording and goes back to knob 8's note alone.
- With REC on, touch and turn knob 8 to rewrite just some of the steps by hand.

#### Replacing the source phrases

The source MIDI is embedded in `dsp.so` at build time (nothing is read from files on
Move). After replacing it you must **rebuild and reinstall**.

1. Put the MIDI files you want (Standard MIDI Files, `.mid`) in `patterns/`.
2. List the files to use in `patterns/patterns.txt`, one per line (1 to 16). The line
   order is the source phrase number. Everything after `#` is a comment.

   ```
   my_riff.mid          # 8 steps from the start of the file
   long_phrase.mid 16   # 8 steps from step 17 (the second column is the start)
   ```

   If `patterns.txt` is deleted, every `.mid` in `patterns/` is used in name order.
3. Build, install, and restart Move.

   ```bash
   scripts/build.sh arm64
   scripts/install.py http://move.local:7700
   ```

Which part of a MIDI file is used:

- Only **eight 1/16 notes from the start position** of each file (anything longer is
  ignored). To use another part of a long file, give the start position (in 1/16 notes)
  in the second column.
- Only **the pitches and their order** are used. Velocity and note length are ignored
  (LUXOR hits every 1/16 at the same strength). Rests are filled with notes as above.
- Notes are snapped to the nearest 1/16. When a step holds several notes (a chord, for
  example) only the loudest one (the first, on a tie) is used. All tracks and channels
  are read together.
- Source phrases are first snapped to **C Phrygian** when loaded, varied, and finally
  fitted to the current ROOT and SCALE. MIDI written in anything other than C Phrygian
  changes slightly, so writing in Phrygian on C keeps the shape best.
- A file with no notes in the eight steps from its start position is not used. File
  names are read up to 31 bytes and up to 256 notes per file.

### Recording knob motion (details)

- With REC on and Move playing, a knob's motion is recorded into the loop **while you
  touch it**. It is recorded at 8 points per 1/16 note, so even a slow filter sweep
  comes back smoothly.
- Touching a knob that already has a recording lets you continue from the value playing
  now (the sound does not jump when you touch it).
- CUTOFF, FM AMT and DRIVE are smoothed between the recorded points; DECAY, P.ENV,
  MOD DEC and PITCH use each point's value as is (so step-by-step changes stay crisp).
- Touching a knob while stopped records nothing. The recording position is the loop
  position counted from Move's transport start.
- Closing the screen with a knob still touched stops the recording automatically.
- Knob touches only reach LUXOR while its screen is open. When you leave the Schwung
  screen for Move's, the knobs control Move itself.

## How it sounds the way it does

```
PITCH (scale note) ──> LXR-02 DRUM voice
   main oscillator ←FM/Mix─ modulator
     ↑ pitch EG (P.ENV / MOD DEC; the FM depth falls with the same EG)
   + transient (Snap / Offset / short sample)
   -> filter (ZDF state variable, 8 types) -> amp EG -> drive
   -> sample rate -> pan
──> LXR-02 FX bus (one type, MIX = dry / wet)
──> DC removal -> output (stereo)
```

- The voice and FX calculations are the LXR-02 firmware source itself (16-bit
  fixed-point maths, 64-sample processing, and the same envelope and LFO update
  intervals as the hardware). The hardware runs at 44.14 kHz and Move at 44.1 kHz, so
  frequencies and times are computed for 44.1 kHz.
- Every hit restarts the amp EG **from the current level** (as on the hardware), so a
  hit arriving while the previous one still rings joins it without a gap.
- Hits are aligned to 64-sample (1.45 ms) boundaries, as on the hardware (at most
  0.73 ms off).
- The hardware's output is AC-coupled by a capacitor, which Move does not have, so a DC
  blocking filter (about 3.5 Hz) sits at the end of the output. FX Tube / Fold leave DC
  behind in silence, and this takes care of it.

### Differences from the original firmware

The port is verified in `tests/lxr_ref/`. A "reference renderer" runs the original
firmware code unmodified on a PC (parameter handling in `MidiParser.c`, `DrumVoice.c`,
`lfo.c`, `modulationNode.c`, `FxBus.c` and more) and plays 24 sounds through it and
through LUXOR's port with the same parameters, the same initial state and the same
random numbers (every waveform, every filter, every transient, every LFO waveform, every
FX, and each parameter's minimum / maximum). **Every sample matches exactly.**

Deliberate changes:

- Two out-of-bounds reads are fixed (`dsp/lxr/patched/`; each fix is described at the
  top of its file). On the hardware both only read a neighbouring memory value that
  never reaches the sound, and they are fixed in a way that gives the same result as
  the hardware.
- Random numbers (the Noise waveform, the LFO's Random) come from a per-instance
  pseudo-random generator instead of the hardware random number generator.
- LFO tempo sync uses Move's tempo (with its fractional part) instead of the hardware's
  integer BPM.
- Velocity is always 127 (LUXOR's steps have no dynamics), so the hardware's velocity
  modulation (VEL DEST / VEL AMT / VEL VOL) is not included.
- Separate settings per FX, the 10 ms crossfade on FX switches, clearing the delay line
  when Delay is re-selected, the 25 ms fade on Stop and the output DC removal are
  LUXOR additions.
- Changing Ring's WAVE plays the new waveform at the right pitch immediately (on the
  hardware the previous waveform's pitch calculation lingers until FREQ is next moved).
- The DECAY knob's curve differs from the hardware's: the knob covers the hardware's
  full range, but more of its travel is given to short decays (see the DECAY row above).
  The voice calculations themselves are unchanged.

## Licence

- **LUXOR as a whole (this repository, `dsp.so` and the distributed package
  `luxor-module.tar.gz`) is GPL-3.0-only** (`LICENSE`), because its engine is a port of
  the LXR-02 firmware (Copyright (C) 2012-2026 Erica Synths, GPL-3.0-only, additional
  terms in `third_party/lxr/NOTICE.md`). LUXOR's own code is Copyright (C) 2026
  Hiro Ikezawa, also GPL-3.0-only.
- As an exception, the Schwung plugin API header (`third_party/schwung/`) stays under its
  original MIT licence.
- If you pass the package on, make this repository's full source available too (a
  condition of GPL-3.0).
- See `THIRD_PARTY_LICENSES.md` for the other bundled code.

**Trademarks**: "Erica Synths", "LXR" and "LXR-02" are names of Erica Synths. LUXOR is
unofficial software, not affiliated with Erica Synths, and is not made, endorsed or
supported by Erica Synths. These names are used only to identify where the ported source
comes from.
