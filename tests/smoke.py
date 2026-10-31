#!/usr/bin/env python3
"""dlopen the real dsp.so through the v2 ABI, as the chain host does."""
import ctypes as c
import json
import pathlib
import random
import sys

LIB = sys.argv[1]
BLOCK = 128
SR = 44100

BPMFN = c.CFUNCTYPE(c.c_float)
BEATFN = c.CFUNCTYPE(c.c_double)


class Host(c.Structure):
    _fields_ = [("api_version", c.c_uint32), ("sample_rate", c.c_int),
                ("frames_per_block", c.c_int), ("mapped_memory", c.c_void_p),
                ("audio_out_offset", c.c_int), ("audio_in_offset", c.c_int),
                ("log", c.c_void_p), ("midi_send_internal", c.c_void_p),
                ("midi_send_external", c.c_void_p), ("get_clock_status", c.c_void_p),
                ("mod_emit_value", c.c_void_p), ("mod_clear_source", c.c_void_p),
                ("mod_host_ctx", c.c_void_p), ("get_bpm", BPMFN),
                ("midi_inject_to_move", c.c_void_p), ("slot_recv_channel", c.c_void_p),
                ("get_beat_position", BEATFN), ("reserved", c.c_void_p * 8)]


assert Host.get_beat_position.offset == 112 and Host.reserved.offset == 120
transport = {"beat": -1.0, "bpm": 120.0}
bpm_cb = BPMFN(lambda: transport["bpm"])
beat_cb = BEATFN(lambda: transport["beat"])


class API(c.Structure):
    _fields_ = [("version", c.c_uint32)] + [(k, c.c_void_p) for k in
               ("create", "destroy", "midi", "set", "get", "error", "render")]


lib = c.CDLL(LIB)
lib.move_plugin_init_v2.restype = c.POINTER(API)
lib.move_plugin_init_v2.argtypes = [c.c_void_p]
bad = Host(api_version=1, sample_rate=48000, frames_per_block=BLOCK)
assert not lib.move_plugin_init_v2(c.byref(bad)), "must refuse a 48 kHz host"
host = Host(api_version=1, sample_rate=SR, frames_per_block=BLOCK, get_bpm=bpm_cb, get_beat_position=beat_cb)
api = lib.move_plugin_init_v2(c.byref(host)).contents
assert api.version == 2

create = c.CFUNCTYPE(c.c_void_p, c.c_char_p, c.c_char_p)(api.create)
destroy = c.CFUNCTYPE(None, c.c_void_p)(api.destroy)
midi = c.CFUNCTYPE(None, c.c_void_p, c.POINTER(c.c_uint8), c.c_int, c.c_int)(api.midi)
set_param = c.CFUNCTYPE(None, c.c_void_p, c.c_char_p, c.c_char_p)(api.set)
get_param = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_char_p, c.c_char_p, c.c_int)(api.get)
render = c.CFUNCTYPE(None, c.c_void_p, c.POINTER(c.c_int16), c.c_int)(api.render)

buf = (c.c_int16 * (BLOCK * 2))()
p = create(b".", None)
assert p


def read(key, size=65536, inst=None):
    out = c.create_string_buffer(size)
    n = get_param(inst or p, key.encode(), out, size)
    return None if n < 0 else out.value.decode()


def write(key, value, inst=None):
    set_param(inst or p, key.encode(), str(value).encode())


def block(inst=None):
    render(inst or p, buf, BLOCK)
    if transport["beat"] >= 0:
        transport["beat"] += BLOCK / (SR * 60 / transport["bpm"])
    return list(buf)


def midi_msg(*b, inst=None):
    midi(inst or p, (c.c_uint8 * len(b))(*b), len(b), 0)


def onsets(env, thresh=2500):
    s = [i for i in range(1, len(env)) if env[i] > thresh and env[i] > 1.8 * env[i - 1]]
    s = [0] + s if env[0] > thresh else s
    return [x for k, x in enumerate(s) if k == 0 or x - s[k - 1] > 10]


def status(inst=None):
    """[lanes with data, step, recording lanes, loop steps, REC pad]"""
    f = read("motion_status", inst=inst).split(",")
    assert len(f) == 5, f
    return [int(x) for x in f]


# --- contract with the shadow UI -------------------------------------------
manifest = json.loads((pathlib.Path(__file__).parent.parent / "module.json").read_text())
meta = json.loads(read("chain_params"))
assert meta == manifest["capabilities"]["chain_params"] and len(meta) == 58
assert "rec" not in {m["key"] for m in meta}, "REC is the pad now, not a parameter"
keys = {m["key"] for m in meta}
assert read("ui_hierarchy") == "", "must be SERVED and EMPTY: opens ui_chain.js"
h = json.loads(read("grid_hierarchy"))
(pathlib.Path(LIB).parent / "grid_hierarchy.json").write_text(read("grid_hierarchy"))   # for test_ui.mjs
assert h["levels"]["root"]["knobs"] == ["volume", "p_env", "fm_amount", "mod_decay",
                                        "cutoff", "drive", "decay", "pitch"]
assert [x["level"] for x in h["levels"]["root"]["params"] if isinstance(x, dict)] == \
    ["osc", "fm", "filter", "fx", "lfo", "scale"]
assert h["levels"]["osc"]["knobs"] == ["wave", "pwm", "attack", "a_slope", "p_slope"]
assert h["levels"]["scale"]["knobs"] == ["root", "scale", "length", "tune", "fine"]
assert "motion" not in h["levels"]
assert h["levels"]["fx"]["knobs"][:2] == ["fx_mix", "fx_type"], "FX page: knob 1 MIX, knob 2 TYPE"
listed = set()
for lvl in h["levels"].values():
    for k in lvl.get("knobs", []):
        assert k in keys, k
        listed.add(k)
assert listed == keys, ("every parameter is on a page", keys - listed)
# FX page: every FX parameter is gated on its own FX type, and the gate
# names a real FX index
fx_types = next(m for m in meta if m["key"] == "fx_type")["options"]
gated = {x["key"]: x["visible_if"] for x in h["levels"]["fx"]["params"] if isinstance(x, dict)}
assert len(gated) == 20 and all(g["param"] == "fx_type" and 1 <= g["equals"] < len(fx_types) for g in gated.values())
per_type = {}
for k, g in gated.items():
    per_type.setdefault(g["equals"], []).append(k)
assert all(len(v) <= 6 for v in per_type.values()), "an FX fits knobs 3-8"
assert sorted(per_type) == [1, 2, 3, 4, 5], "every FX but Off has its knobs"
assert read("no_such_key") is None
for m in meta:                                   # defaults are what get_param reports
    v = read(m["key"])
    assert abs(float(v) - m["default"]) < 1e-4, (m["key"], v)
    if m["type"] == "enum":
        assert v == str(m["default"]), "enums speak indices"
# PITCH: the knob IS a note of the scale -- an index, every detent a new note
assert read("pitch") == "7" and read("pitch_note") == "C2", read("pitch_note")
names = read("pitch_names").split(",")
assert len(names) == 29 and names[0] == "C1" and names[7] == "C2" and names[9] == "Eb2" and names[-1] == "C5", names
assert all(n[:-1] in ("C", "Db", "Eb", "F", "G", "Ab", "Bb") for n in names), "C Phrygian only"
st = status()
assert st == [0, -1, 0, 16, 0], st
write("pitch", 9)
assert read("pitch_note") == "Eb2"
write("pitch", 40)                               # past the scale's top: its top note
assert read("pitch") == "28" and read("pitch_note") == "C5"
write("pitch", 8.4)                              # an int knob: rounded
assert read("pitch") == "8" and read("pitch_note") == "Db2"
# a new scale re-snaps the knob to the nearest note, and PITCH's range follows
write("pitch", 9)                                # Eb2
write("scale", 3)                                # C major: D2 / E2 tie -> D2
assert read("pitch_note") == "D2", read("pitch_note")
cp = json.loads(read("chain_params"))
assert next(m for m in cp if m["key"] == "pitch")["max"] == 28
write("scale", 11)                               # chromatic: 49 notes
assert next(m for m in json.loads(read("chain_params")) if m["key"] == "pitch")["max"] == 48
assert read("pitch_note") == "D2" and len(read("pitch_names").split(",")) == 49
write("root", 2)                                 # D chromatic: same notes
assert read("pitch_note") == "D2"
write("scale", 0); write("root", 0)
write("pitch", 7)
assert read("pitch_note") == "C2" and json.loads(read("chain_params")) == manifest["capabilities"]["chain_params"]

# --- transport stopped: never sounds; keepalive DC only ------------------------
for _ in range(20):
    assert all(x == 6 for x in block()), "stopped: 6 LSB keepalive DC only"
midi_msg(0x90, 36, 127)                          # notes are ignored
for _ in range(20):
    assert all(x == 6 for x in block()), "a note does not play it"
write("volume", 0)
for _ in range(20):
    assert not any(block()), "VOLUME 0: exact silence, no keepalive"
write("volume", 0.75)

# --- Play: a hit on every 1/16, locked to the host beat -------------------------
write("decay", 0)                                # short, so every onset is visible
for bpm, secs, expect_gap in [(120, 2, {43, 44}), (90, 2, {57, 58})]:
    transport.update(beat=0.0, bpm=bpm)
    env = [max(abs(x) for x in block()) for _ in range(int(secs * SR / BLOCK))]
    o = onsets(env)
    assert len(o) == int(secs * bpm / 60 * 4), (bpm, len(o), o)
    assert {o[k + 1] - o[k] for k in range(len(o) - 1)} <= expect_gap, (bpm, o)
    assert status()[1] >= 0
    transport["beat"] = -1.0
    tail = [max(abs(x) for x in block()) for _ in range(40)]
    assert max(tail[10:]) <= 40, ("stop: silent within ~30 ms (the 25 ms fade)", tail[:12])
    assert status()[1] == -1
# --- trigger pads: a step whose pad is off does not fire ---------------------------
assert read("gate_status") == "1" * 16
for step in range(1, 16, 2):                     # steps 2, 4, ... 16 off
    write("gate_toggle", step)
assert read("gate_status") == "10" * 8
transport.update(beat=0.0, bpm=120)
env = [max(abs(x) for x in block()) for _ in range(int(2 * SR / BLOCK))]
o = onsets(env)
assert len(o) == 8 and {o[k + 1] - o[k] for k in range(len(o) - 1)} <= {86, 87, 88}, o
transport["beat"] = -1.0
for _ in range(40):
    block()
for bad in ["16", "-1", "1.5", "x", ""]:
    write("gate_toggle", bad)
assert read("gate_status") == "10" * 8
for step in range(1, 16, 2):
    write("gate_toggle", step)
assert read("gate_status") == "1" * 16
write("decay", next(m for m in meta if m["key"] == "decay")["default"])

# --- MIDI clock fallback (a host without get_beat_position) ----------------------
host2 = Host(api_version=1, sample_rate=SR, frames_per_block=BLOCK, get_bpm=bpm_cb)
lib.move_plugin_init_v2(c.byref(host2))
q = create(b".", None)
write("decay", 0, inst=q)
midi_msg(0xFA, inst=q)
env, sent = [], 0
tick = SR * 0.5 / 24                             # 24 PPQN at 120 BPM: 551.25 samples
for i in range(int(1.0 * SR / BLOCK)):
    while sent * tick <= i * BLOCK:
        midi_msg(0xF8, inst=q)
        sent += 1
    render(q, buf, BLOCK)
    env.append(max(abs(x) for x in buf))
assert len(onsets(env)) >= 7, ("MIDI clock drives the 1/16s", len(onsets(env)))
midi_msg(0xFC, inst=q)
destroy(q)
lib.move_plugin_init_v2(c.byref(host))

# --- the FX bus through the plugin: a delay repeats after the hit ------------
fxi = create(b".", None)
write("fx_type", 4, inst=fxi); write("fx_mix", 1.0, inst=fxi); write("decay", 0, inst=fxi)
write("dly_time", 0.3, inst=fxi); write("dly_range", 1.0, inst=fxi); write("dly_fdbk", 0.6, inst=fxi)
for g in range(16):
    if g: write("gate_toggle", g, inst=fxi)           # one hit per bar
transport.update(beat=0.0, bpm=120.0)
env = [max(abs(x) for x in block(fxi)) for _ in range(int(1.0 * SR / BLOCK))]
transport["beat"] = -1.0
for _ in range(40):
    block(fxi)
assert len(onsets(env, 800)) >= 3, ("hit + echoes", onsets(env, 800))
destroy(fxi)

# --- params: clamp, reject junk ------------------------------------------------
for m in meta:
    k = m["key"]
    lo = m.get("min", 0)
    hi = m.get("max", len(m.get("options", [])) - 1)
    write(k, 1e6)
    assert abs(float(read(k)) - hi) < 1e-4, k
    for junk in ["nan", "inf", "0.5x", "", " "]:
        write(k, junk)
        assert abs(float(read(k)) - hi) < 1e-4, (k, junk)
    write(k, -1e6)
    assert abs(float(read(k)) - lo) < 1e-4, k
    write(k, m["default"])

# --- touch automation through the plugin ----------------------------------------
mo = create(b".", None)
assert read("write", inst=mo) == "0", "the REC pad starts off"
transport.update(beat=0.0, bpm=120.0)


def to_pos(pos16, inst):
    """Render until the playhead has passed pos16 (1/16s since Play)."""
    while transport["beat"] * 4 < pos16:
        block(inst)


to_pos(1.0, mo)
write("touch", "cutoff,1", inst=mo)              # REC pad off: a touch writes nothing
to_pos(2.0, mo)
write("touch", "cutoff,0", inst=mo)
assert status(mo)[0] == 0 and status(mo)[2] == 0, status(mo)
write("write", 1, inst=mo)                       # the REC pad
assert status(mo)[4] == 1
write("cutoff", 0.2, inst=mo)
write("touch", "cutoff,1", inst=mo)
assert status(mo)[2] == 1 << 4, status(mo)      # recording lane 4 = CUTOFF
while transport["beat"] * 4 < 3.0:              # sweep over one 1/16, block by block
    write("cutoff", 0.2 + 0.6 * (transport["beat"] * 4 - 2.0), inst=mo)
    block(mo)
write("touch", "cutoff,0", inst=mo)
write("cutoff", 0.5, inst=mo)                    # the knob, left elsewhere
st = status(mo)
assert st[0] == 1 << 4 and st[2] == 0, st
to_pos(16 + 2.5, mo)                              # next bar, mid-step
eff = float(read("eff_cutoff", inst=mo))
assert 0.4 < eff < 0.6, ("plays the curve inside the 1/16", eff)
assert float(read("cutoff", inst=mo)) == 0.5, "the knob keeps the hand's value"
to_pos(16 + 6.0, mo)
assert float(read("eff_cutoff", inst=mo)) == 0.5, "an untouched region plays the knob"
write("touch", "fm_amount,1", inst=mo)           # a touch the UI never restates...
assert status(mo)[2] == 1 << 2
for _ in range(int(0.55 * SR / BLOCK)):
    block(mo)
assert status(mo)[2] == 0, "...ends by itself after 0.5 s"
write("motion_clear", "fm_amount", inst=mo)
write("touch", "volume,1", inst=mo)              # VOLUME is never recorded
assert status(mo)[2] == 0
write("touch", "volume,0", inst=mo)
state = read("state", inst=mo)
doc = json.loads(state)
assert doc["v"] == 4 and len(doc["p"]) == 58 and doc["m"].startswith("cutoff:") and ";" not in doc["m"]
assert doc["g"] == 65535 and "write" not in doc, "the REC pad is never saved"
assert len(state) < 16384 - 64, "fits the chain's MAX_SYNTH_STATE_LEN even with all lanes"
m2 = create(b".", None)
write("state", state, inst=m2)
assert read("state", inst=m2) == state
destroy(m2)
write("motion_clear", "cutoff", inst=mo)
assert status(mo)[0] == 0
for junk in ["cutoff", "cutoff,2", "nope,1", ",1", "cutoff,", "x" * 100 + ",1"]:
    write("touch", junk, inst=mo)
    assert status(mo)[2] == 0, junk
transport["beat"] = -1.0
block(mo)
destroy(mo)

# --- state: all lanes, invalid documents, fuzz ----------------------------------
full = create(b".", None)
write("write", 1, inst=full)
write("gate_toggle", 3, inst=full)
transport.update(beat=0.0, bpm=120.0)
lanes = ["decay", "p_env", "fm_amount", "mod_decay", "cutoff", "drive", "pitch"]
for k in lanes:
    write("touch", k + ",1", inst=full)
for _ in range(int(4 * 2 * SR / BLOCK) + 2):     # a whole 4-bar loop under seven fingers
    block(full)
write("length", 3, inst=full)
for _ in range(int(8 * SR / BLOCK) + 2):
    block(full)
for k in lanes:
    write("touch", k + ",0", inst=full)
state = read("state", inst=full)
assert status(full)[0] == 127, status(full)
assert len(state) < 16384 - 64, len(state)
doc = json.loads(state)
other = create(b".", None)
write("state", json.dumps(doc, indent=2), inst=other)
assert read("state", inst=other) == state
before = read("state", inst=other)
lane0 = doc["m"].split(";")[0]
assert json.loads(state)["g"] == 65535 ^ 8
for invalid in ["{}", "{", "", "[]", state[:-2], state.replace('"v":4', '"v":5'),
                state.replace('"v":4', '"v":3'),                   # v3 carries 16 values
                state.replace('"v":4', '"v":2'),                   # v2 carries 17 values
                state.replace('"g":' + str(65535 ^ 8), '"g":70000'),
                state.replace(',"g":' + str(65535 ^ 8), ''),
                state.replace('"p":[', '"p":[999,'), state.replace('"p":[', '"p":[1,'),
                state.replace(lane0, lane0[:-1]), state.replace(lane0, lane0 + "A"),
                state.replace(lane0, lane0.replace(":", ":!", 1)),
                state.replace(lane0, "volume" + lane0[lane0.index(":"):]),
                state.replace(lane0, lane0 + ";" + lane0),
                state.replace('"m":"', '"m":')]:
    write("state", invalid, inst=other)
    assert read("state", inst=other) == before, invalid[:80]
rng = random.Random(7)
for _ in range(3000):
    junk = "".join(rng.choice('{}[]:;,."0123456789e+-vpmAZaz_ ') for _ in range(rng.randrange(160)))
    write("state", junk, inst=other)
    assert read("state", inst=other) == before
write("state", '{"v":4,"p":' + json.dumps(doc["p"]) + ',"m":"","g":1}', inst=other)
assert status(other)[0] == 0, "an empty motion string clears the lanes"
assert read("gate_status", inst=other) == "1" + "0" * 15
# every FX keeps its own settings through a save
write("fx_type", 4, inst=other); write("dly_fdbk", 0.9, inst=other)
write("fx_type", 1, inst=other); write("drv_drive", 0.1, inst=other)
again = create(b".", None)
write("state", read("state", inst=other), inst=again)
assert read("fx_type", inst=again) == "1" and read("dly_fdbk", inst=again) == "0.9000"
assert read("drv_drive", inst=again) == "0.1000"
destroy(again)

# The pre-LXR LUXOR (v1..v3): only what still means the same is carried --
# VOLUME, PITCH (+ its lane), ROOT, SCALE, LENGTH, the pads. Its sound knobs
# drove another instrument and are NOT converted: they start at the defaults.
alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
def lane_of(q):
    return (alphabet[q >> 6] + alphabet[q & 63]) * 8 + "__" * 504
#      volume vcf_dec vca_dec fm   vcf_env cutoff res/drv pitch root scale p_env p_dec vco2tn vco2 noise length
old3 = [0.6,  0.9,    0.1,    0.9, 0.2,    0.05,  0.95,   9,    0,   0,    0.3,  0.4,  7.3,   0.6, 0.05, 2]
q12 = round(12 / 48 * 4094)                       # a v3 pitch lane point: index 12
write("state", '{"v":3,"p":' + json.dumps(old3) + ',"m":"cutoff:' + lane_of(1000) + ';pitch:' +
      lane_of(q12) + '","g":' + str(65535 ^ 2) + '}', inst=other)
assert read("volume", inst=other) == "0.6000" and read("pitch", inst=other) == "9"
assert read("length", inst=other) == "2" and read("gate_status", inst=other) == "1011111111111111"
assert status(other)[0] == 1 << 6, ("only the PITCH lane is carried", status(other))
for m in meta:
    if m["key"] not in ("volume", "pitch", "root", "scale", "length"):
        assert abs(float(read(m["key"], inst=other)) - m["default"]) < 1e-4, (m["key"], "a default")
assert read("fx_type", inst=other) == "0"
for invalid3 in [old3[:-1], old3 + [0], old3[:7] + [29] + old3[8:], old3[:15] + [4], old3[:8] + [12] + old3[9:]]:
    before3 = read("state", inst=other)
    write("state", '{"v":3,"p":' + json.dumps(invalid3) + ',"m":"","g":1}', inst=other)
    assert read("state", inst=other) == before3, invalid3
# v1 (0.1.0: PITCH a 0..1 knob, + REC) and v2 (0.2.0: PITCH semitones above
# C1, + REC) load as the note they played -- the knob and the pitch lane
legacy = old3[:] + [1]
legacy[7] = 15                                    # v2: MIDI 39 = Eb2 = index 9
pq = round(26 / 48 * 4094)                        # v2 lane: 26 semitones = D3 -> Db3 (tie down)
lane = lane_of(pq)
write("state", '{"v":2,"p":' + json.dumps(legacy) + ',"m":"pitch:' + lane + ';vcf_env:' + lane + '"}', inst=other)
assert read("pitch", inst=other) == "9" and read("pitch_note", inst=other) == "Eb2"
assert read("gate_status", inst=other) == "1" * 16, "old states: every step fires"
assert status(other)[0] == 1 << 6
transport.update(beat=0.0, bpm=120.0)
block(other)
assert read("eff_pitch", inst=other) == "15", read("eff_pitch", inst=other)   # Db3 = index 15
transport["beat"] = -1.0
block(other)
legacy[7] = 0.3                                   # v1: 0.3 x 48 = 14 -> D2 -> Db2 (tie down) = 8
write("state", '{"v":1,"p":' + json.dumps(legacy) + ',"m":""}', inst=other)
assert read("pitch", inst=other) == "8", read("pitch", inst=other)
legacy[7] = 1.5                                   # v1 PITCH out of 0..1
write("state", '{"v":1,"p":' + json.dumps(legacy) + ',"m":""}', inst=other)
assert read("pitch", inst=other) == "8"
write("state", '{"v":2,"p":' + json.dumps(legacy) + ',"m":"nope:' + lane + '"}', inst=other)
assert read("pitch", inst=other) == "8", "an unknown lane name refuses the document"

# --- the RANDOM pad: a Phrase Gen phrase into PITCH, scale notes only ----------
rnd = create(b".", None)
write("randomize", 1, inst=rnd)
assert status(rnd)[0] == 1 << 6, "RANDOM writes the PITCH lane only"
st = json.loads(read("state", inst=rnd))
pitch_lane = st["m"].split(":")[1]
vals = [alphabet.index(pitch_lane[i]) * 64 + alphabet.index(pitch_lane[i + 1]) for i in range(0, 1024, 2)]
steps = [vals[k * 8:(k + 1) * 8] for k in range(16)]
assert all(len(set(v)) == 1 for v in steps), "a step holds one note"
assert all(v != 4095 for v in vals[:128]) and all(v == 4095 for v in vals[128:]), "one bar, as LENGTH"
idx = [round(v[0] / 4094 * 48) for v in steps]
assert all(0 <= i <= 28 for i in idx) and len(set(idx)) > 1, idx
transport.update(beat=0.0, bpm=120.0)
seen = set()
for _ in range(int(2 * SR / BLOCK)):
    block(rnd)
    seen.add(int(read("eff_pitch", inst=rnd)))
transport["beat"] = -1.0
block(rnd)
assert seen == set(idx), (seen, idx)
write("randomize", 1, inst=rnd)
assert json.loads(read("state", inst=rnd))["m"] != st["m"], "a new press, a new phrase"
# The register follows the knob: folding alone centred a C1 knob on G2.
c1 = []
write("pitch", 0, inst=rnd)
for _ in range(50):
    write("randomize", 1, inst=rnd)
    lane_c1 = json.loads(read("state", inst=rnd))["m"].split(":")[1]
    c1 += [round((alphabet.index(lane_c1[i]) * 64 + alphabet.index(lane_c1[i + 1])) / 4094 * 48) for i in range(0, 256, 16)]
c1.sort()
assert c1[0] == 0 and c1[-1] <= 10, (c1[0], c1[-1])  # C1..F2, C Phrygian
assert c1[len(c1) // 2] < 7, "a C1 knob's phrases sit below C2"
destroy(rnd)
empty = create(b".", None)
for inst in (full, other, empty):
    destroy(inst)
transport["beat"] = -1.0

# --- bounds -------------------------------------------------------------------
for key in ["state", "chain_params", "grid_hierarchy", "motion_status", "cutoff"]:
    small = c.create_string_buffer(3)
    assert get_param(p, key.encode(), small, 3) == -1 and small.value == b""
for frames in [1, 17, 128]:
    guarded = (c.c_int16 * (2 * frames + 2))()
    guarded[0] = 1234
    guarded[-1] = 5678
    render(None, c.cast(c.byref(guarded, 2), c.POINTER(c.c_int16)), frames)
    assert guarded[0] == 1234 and guarded[-1] == 5678
    assert not any(guarded[1:-1])
transport.update(beat=0.0, bpm=175.0)
for i in range(800):                              # fast tempo, bounded
    out = block()
    assert all(-32768 <= x <= 32767 for x in out)
transport["beat"] = -1.0

# --- pool ---------------------------------------------------------------------
handles = [p] + [create(b".", None) for _ in range(5)]
assert all(handles) and len(set(handles)) == 6
assert not create(b".", None), "pool is fixed; the 7th is refused, not allocated"
for hnd in handles:
    destroy(hnd)
assert create(b".", None)
print("PASS: v2 ABI, hierarchy + chain_params, auto 1/16 (host beat + MIDI clock), stop, "
      "VOLUME 0, touch automation, state, bounds, pool")
