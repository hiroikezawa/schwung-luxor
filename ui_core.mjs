/*
 * LUXOR - Signal Chain UI core.
 *
 * WHY THIS FILE EXISTS: Schwung's stock knob grid runs no module code, so the
 * DSP would never learn that a knob is being TOUCHED -- and touch is the
 * automation write window. So the DSP serves `ui_hierarchy` empty (the shadow
 * UI then opens ui_chain.js) and this file drives Schwung's OWN page engine
 * (page_controller.mjs) itself: same grid, same pages, plus the knobs' touch
 * sensors and the pads. Same arrangement as LXRDRUM (module id fmdrum).
 *
 * The sound is the LXR-02 firmware's DRUM voice and FX (dsp/lxr); the pages
 * follow the LXR's own: Main (the eight that move the sound most), Osc, FM,
 * Filter, FX, LFO, then the set-and-forget page: ROOT, SCALE, LENGTH and
 * the LXR's TUNE / FINE.
 *
 * With the REC pad on, touching a sound knob of the main page (knobs 2-8)
 * while Move plays records its movement for as long as the finger stays on
 * it; Shift + touch clears that knob, Shift + touch on VOLUME clears all
 * seven. VOLUME itself is never recorded.
 *
 * THE FX PAGE is knob 1 MIX, knob 2 FX TYPE, and from knob 3 on the
 * parameters of the FX in use and nothing else: the DSP declares every FX's
 * parameters with a visible_if on fx_type and this file is the evaluator the
 * page engine asks (load({ visible })), so changing the type re-plans the
 * page and the knobs a type does not have are simply not there. The DSP owns the
 * recording (dsp/bk_motion.c); this only reports touches and shows the result:
 *   - an automated knob is drawn as a modulated one (pointer on the knob's
 *     own value, mark at the value playing now) via the controller's own
 *     isModulated / `:effective` path;
 *   - the header shows the loop step, ARM while REC is on, REC while a finger
 *     writes, and the note while PITCH is held.
 *
 * THE PADS are taken (host_pad_block) while this screen is up -- Schwung only
 * lets a module hold them then, and hands them back when the display closes:
 *   top-right (99)      REC: touches record motion. Solid red on, dim off.
 *   next to it (98)     RANDOM: a new Phrase Gen phrase into PITCH's steps.
 *   second row (84-91)  eight PRESETS: tap recalls, Shift+tap saves the
 *                       whole state (sound, motion, steps, scale). Off =
 *                       empty, dim = saved, white = last recalled/saved.
 *                       Stored in PRESET_PATH, shared by every slot.
 *   bottom two rows     the 16 trigger steps (68-75 = 1-8, 76-83 = 9-16;
 *   (68-83)             dark past a 1/2-bar loop). A longer loop repeats
 *                       them every bar. The step playing is white.
 * Same layout and colours as MANIAC / LXRDRUM.
 *
 * PITCH is an INTEGER index into the notes of the Scale page's root + scale:
 * the knob itself is set in the scale, nothing is quantized afterwards. It is
 * shown as a note name through the controller's formatValue hook, and its
 * range follows the scale, so after a root/scale change the contract is
 * re-read.
 *
 * SHIFT IS NOT MIDI HERE: in shadow mode the shim swallows CC 49 and
 * publishes the state in shared memory (shadow_get_shift_held).
 *
 * Every dependency is injected (ui_chain.js binds the device, the tests bind
 * fakes), so nothing here touches a global.
 */

/* Bit k of the DSP's masks = MOTION_KEYS[k] (bk_motion.c's lane order). A
 * lane records from whatever page its knob is on. */
export const MOTION_KEYS = ["decay", "p_env", "fm_amount", "mod_decay", "cutoff",
                            "drive", "pitch"];
export const MAIN_KEYS = ["volume", "p_env", "fm_amount", "mod_decay",
                          "cutoff", "drive", "decay", "pitch"];

/* One IPC read costs ~2.8 ms (a whole page render is 1.68 ms), so the status
 * is polled, not read per frame: every 6 ticks is ~10 Hz, enough to follow a
 * 1/16 at 150 BPM. */
export const STATUS_EVERY_TICKS = 6;
/* The note names change only with the Scale page (or a state load), so they
 * are re-read at once after a root/scale write and otherwise every ~2 s. */
export const NAMES_EVERY_TICKS = 120;
/* Held touches are restated this often; the DSP lets a touch go after 500 ms
 * without one, so a screen closed under a finger cannot keep recording. */
export const HEARTBEAT_MS = 150;

export const PAD_FIRST = 68, PAD_LAST = 99;
export const GATE_LAST = 83, RANDOM_PAD = 98, RECORD_PAD = 99;
export const PRESET_FIRST = 84, PRESETS = 8;
/* Move pad palette indices, as MANIAC / LXRDRUM use them. */
export const LED = { RECORD_ON: 127, RECORD_OFF: 65, RANDOM: 21, PRESSED: 120,
                     PLAYHEAD: 120, GATE_ON: 11, GATE_OFF: 1, OFF: 0,
                     PRESET_SAVED: 73, PRESET_CURRENT: 120 };
const LEDS_PER_TICK = 8;              /* 32 pads repainted every 4 frames */
const NOTICE_MS = 1200;

/* ---- the preset pads ---------------------------------------------------- */

export const SETTINGS_DIR = "/data/UserData/schwung/luxor";
export const PRESET_PATH = `${SETTINGS_DIR}/presets.json`;

/* {"v":1,"presets":[8 x state string | null]}. Anything unreadable starts
 * empty rather than refusing to run. */
export function parsePresets(text) {
    const out = new Array(PRESETS).fill(null);
    if (typeof text !== "string" || !text) return out;
    let doc;
    try { doc = JSON.parse(text); } catch (e) { return out; }
    const a = doc && doc.v === 1 && Array.isArray(doc.presets) ? doc.presets : [];
    for (let i = 0; i < PRESETS; i++) out[i] = typeof a[i] === "string" && a[i] ? a[i] : null;
    return out;
}

export function serializePresets(list) {
    return JSON.stringify({ v: 1, presets: list });
}

export function createUi(deps) {
    const {
        createController, decodeInput, applyInput,
        getParam, setParam, announce = () => {},
        layout = () => undefined, draw, clear,
        trailingMenus = null, runAction = null, now = () => Date.now(),
        shiftHeld = () => false,
        displayOn = () => true, padBlock = () => {},
        sendLed = () => {}, padSnapshot = () => null,
        readFile = () => null, writeFile = () => false, ensureDir = () => false,
    } = deps;

    const st = {
        controller: null,
        shift: false,
        shiftCc: false,   /* a host that does forward CC 49 */
        tick: 0,
        mask: 0,          /* lanes that hold automation */
        step: -1,         /* loop step, -1 stopped */
        recording: 0,     /* lanes being written, as the DSP saw them */
        len: 16,          /* loop: 8, 16, 32 or 64 steps */
        write: 0,         /* REC pad */
        gates: "1".repeat(16),
        names: null,      /* PITCH note names for the scale in use */
        namesRaw: null,
        keyDirty: false,  /* root/scale written: re-read names + contract */
        slotKey: {},      /* knob slot -> lane it touched (the page may change under the finger) */
        lastBeat: 0,
        visible: false,
        heldPads: new Set(),
        ledCursor: 0,
        notice: "", noticeUntil: 0,
        dirReady: false,
        presets: null,    /* loaded on first use */
        preset: -1,       /* last recalled / saved */
    };

    const bare = (fullKey) => {
        const i = fullKey.indexOf(":");
        return i < 0 ? fullKey : fullKey.slice(i + 1);
    };
    const bit = (key) => {
        const i = MOTION_KEYS.indexOf(key);
        return i < 0 ? 0 : 1 << i;
    };
    const isAutomated = (key) => (st.mask & bit(key)) !== 0;

    /* The controller asks for `synth:<key>`; the host's per-slot shims add the
     * prefix themselves, so it is stripped here. Redirected: the hierarchy
     * lives under grid_hierarchy (see bk_plugin.c), and the value an
     * automated knob is PLAYING is served as eff_<key>, because the chain
     * answers `:effective` itself and never asks the plugin. */
    function ioGet(fullKey) {
        const k = bare(fullKey);
        if (k === "ui_hierarchy") return getParam("grid_hierarchy");
        if (k.endsWith(":effective")) return getParam("eff_" + k.slice(0, -10));
        if (k.endsWith(":modulated")) return isAutomated(k.slice(0, -10)) ? "1" : "0";
        return getParam(k);
    }

    function pollStatus() {
        const raw = getParam("motion_status");
        if (typeof raw === "string" && raw.length) {       /* null / "": keep the last answer */
            const f = raw.split(",").map((x) => parseInt(x, 10));
            if (f.length === 5 && f.every(Number.isFinite) && [8, 16, 32, 64].includes(f[3]))
                [st.mask, st.step, st.recording, st.len, st.write] = [f[0], f[1], f[2], f[3], f[4] ? 1 : 0];
        }
        const g = getParam("gate_status");
        if (typeof g === "string" && /^[01]{16}$/.test(g)) st.gates = g;
    }

    /* Returns true when the names changed, i.e. the scale did. */
    function pollNames() {
        const raw = getParam("pitch_names");
        if (typeof raw !== "string" || !raw.length || raw === st.namesRaw) return false;
        st.namesRaw = raw;
        st.names = raw.split(",");
        return true;
    }

    function noteOf(raw) {
        const i = Math.round(Number(raw));
        return st.names && Number.isFinite(i) && i >= 0 && i < st.names.length ? st.names[i] : null;
    }

    /* visible_if, for the page engine. The value is the controller's own (it
     * updates on the turn, before the write that makes it re-plan), else the
     * DSP's. A condition this file cannot read is visible: the planner's own
     * fail-open default. */
    function conditionValue(key) {
        const v = st.controller && st.controller.state && st.controller.state.values
            ? st.controller.state.values[key] : undefined;
        if (v !== undefined && v !== null && v !== "") return Number(v);
        const raw = getParam(key);
        return typeof raw === "string" && raw.length ? Number(raw) : NaN;
    }

    function visible(cond) {
        if (!cond || typeof cond !== "object" || !cond.param) return true;
        const v = conditionValue(cond.param);
        if (!Number.isFinite(v)) return true;
        if ("equals" in cond) return v === Number(cond.equals);
        if ("not_equals" in cond) return v !== Number(cond.not_equals);
        if ("gt" in cond) return v > Number(cond.gt);
        if ("lt" in cond) return v < Number(cond.lt);
        if (cond.truthy) return v !== 0;
        if (cond.falsy || cond.falsey) return v === 0;
        return true;
    }

    function formatValue(fullKey, raw) {
        return bare(fullKey) === "pitch" ? noteOf(raw) : null;
    }

    function shiftNow() {
        return st.shiftCc || !!shiftHeld();
    }

    /* Follow the shared-memory shift, and give the page engine the same
     * reveal-values the stock grid gives it. */
    function syncShift() {
        const next = shiftNow();
        if (next === st.shift) return;
        st.shift = next;
        if (st.controller) applyInput(st.controller, { type: "shift", down: next }, { nowMs: now(), reveal: true });
    }

    function init() {
        st.controller = createController({
            getParam: ioGet,
            setParam: (fullKey, value) => {
                const k = bare(fullKey);
                if (k === "root" || k === "scale") st.keyDirty = true;
                return setParam(k, value);
            },
            formatValue,
            announce,
            isModulated: (fullKey) => isAutomated(bare(fullKey)),
            trailingMenus: trailingMenus ? () => trailingMenus() || [] : undefined,
            now,
        });
        pollNames();
        st.controller.load({ slot: 0, component: "synth", prefix: "synth", visible });
        pollStatus();
    }

    /* Touches this UI is holding, merged with what the DSP last reported: the
     * header must answer under the finger, not a poll later. */
    function heldMask() {
        let m = st.recording;
        for (const key of Object.values(st.slotKey)) m |= bit(key);
        return m;
    }

    function showNotice(text) {
        st.notice = text;
        st.noticeUntil = now() + NOTICE_MS;
    }

    function title() {
        if (now() < st.noticeUntil) return st.notice;
        /* The note under the finger, from the controller's value cache: a
         * read per frame would cost more than the whole frame. */
        let head = "LUXOR";
        const held = heldMask();
        if (held & bit("pitch")) {
            const n = st.controller ? noteOf(st.controller.state.values.pitch) : null;
            if (n) head = "PITCH " + n;
        }
        if (st.step < 0) return st.write ? head + " ARM" : head;
        const pos = `${st.step + 1}/${st.len}`;
        if (st.write && held) return `${head} REC ${pos}`;
        return st.write ? `${head} ARM ${pos}` : `${head} ${pos}`;
    }

    /* Trigger pads show the bar the loop is in: step 17 of a 2-bar loop lights
     * pad 1. */
    function padColor(note) {
        if (note <= GATE_LAST) {
            const step = note - PAD_FIRST;
            if (step >= Math.min(st.len, 16)) return LED.OFF;
            if (st.step >= 0 && step === st.step % 16) return LED.PLAYHEAD;
            return st.gates[step] === "1" ? LED.GATE_ON : LED.GATE_OFF;
        }
        if (note >= PRESET_FIRST && note < PRESET_FIRST + PRESETS) {
            const i = note - PRESET_FIRST;
            const saved = !!(st.presets && st.presets[i]);
            if (!saved) return LED.OFF;
            return st.heldPads.has(note) || st.preset === i ? LED.PRESET_CURRENT : LED.PRESET_SAVED;
        }
        if (note === RANDOM_PAD) return st.heldPads.has(note) ? LED.PRESSED : LED.RANDOM;
        if (note === RECORD_PAD) return st.write ? LED.RECORD_ON : LED.RECORD_OFF;
        return LED.OFF;
    }

    function paintPads() {
        for (let i = 0; i < LEDS_PER_TICK; ++i) {
            const note = PAD_FIRST + (st.ledCursor++ % 32);
            sendLed(note, padColor(note));
        }
    }

    /* Move's own pad colours come back when we let go of the pads. */
    function restorePads() {
        const snap = padSnapshot();
        for (let n = PAD_FIRST; n <= PAD_LAST; ++n) sendLed(n, (snap && snap[String(n)]) || 0);
    }

    function tick() {
        const c = st.controller;
        if (!c) return;
        loadPresets();
        if (!displayOn()) { if (st.visible) releaseAll(); return; }
        st.visible = true;
        padBlock(true);                   /* restated every frame: the shim drops it on its own */
        syncShift();
        if (++st.tick % STATUS_EVERY_TICKS === 0) pollStatus();
        /* After the controller's own write (it flushes inside c.tick), so a
         * root/scale turn is re-read on the next frame, never mid-flush. */
        if (st.keyDirty || st.tick % NAMES_EVERY_TICKS === 0) {
            st.keyDirty = false;
            if (pollNames()) c.reloadIfChanged();   /* PITCH's range follows the scale */
        }
        const held = Object.values(st.slotKey);
        if (held.length && now() - st.lastBeat >= HEARTBEAT_MS) {
            st.lastBeat = now();
            for (const key of held) setParam("touch", `${key},1`);
        }
        const l = layout();
        if (l) c.setLayout(l);
        c.tick();
        clear();
        c.render(draw, { title: title() });
        c.renderOverlays(draw, { clearScreen: clear });
        paintPads();
    }

    /* A knob's key on THIS page. The main page's sound knobs record; VOLUME
     * and every other page's knobs do not. The release goes to the lane the
     * touch began on, even if the page changed since. */
    function touch(slot, down) {
        if (!down) {
            const key = st.slotKey[slot];
            if (!key) return;
            delete st.slotKey[slot];
            setParam("touch", `${key},0`);
            return;
        }
        const full = st.controller.keyAt(slot);
        const key = full ? bare(full) : null;
        if (!key) return;
        if (key === "volume") {
            /* never recorded; Shift + touch here clears every lane */
            if (st.shift) {
                setParam("motion_clear", "all");
                st.mask = 0;
                announce("All automation cleared");
            }
            return;
        }
        if (!MOTION_KEYS.includes(key)) return;
        if (st.shift) {
            setParam("motion_clear", key);
            st.mask &= ~bit(key);
            announce(`${key} automation cleared`);
            return;
        }
        setParam("touch", `${key},1`);
        st.slotKey[slot] = key;
        st.lastBeat = now();
    }

    function loadPresets() {
        if (!st.presets) st.presets = parsePresets(readFile(PRESET_PATH));
    }

    /* Shift+tap stores the whole state; a tap applies it, playing or not. */
    function onPreset(i) {
        loadPresets();
        if (shiftNow()) {
            const text = getParam("state");
            if (typeof text !== "string" || !text) { showNotice("SAVE FAILED"); return; }
            const next = st.presets.slice();
            next[i] = text;
            if (!st.dirReady) st.dirReady = !!ensureDir(SETTINGS_DIR);
            if (st.dirReady && writeFile(PRESET_PATH, serializePresets(next))) {
                st.presets = next;
                st.preset = i;
                showNotice(`SAVED PRESET ${i + 1}`);
                announce(`Preset ${i + 1} saved`);
            } else {
                showNotice("SAVE FAILED");
            }
            return;
        }
        const text = st.presets[i];
        if (!text) { showNotice(`EMPTY ${i + 1} (SHIFT+PAD SAVES)`); return; }
        if (!setParam("state", text)) { showNotice("LOAD FAILED"); return; }
        st.preset = i;
        showNotice(`PRESET ${i + 1}`);
        announce(`Preset ${i + 1}`);
        /* Every knob and the scale may have changed under us. */
        st.keyDirty = true;
        pollStatus();
        if (st.controller && typeof st.controller.revalue === "function") st.controller.revalue();
    }

    function onPad(note, down) {
        if (!down) { st.heldPads.delete(note); sendLed(note, padColor(note)); return; }
        if (st.heldPads.has(note)) return;
        st.heldPads.add(note);
        if (note === RECORD_PAD) {
            st.write = st.write ? 0 : 1;
            setParam("write", st.write);
            announce(st.write ? "Motion record on" : "Motion record off");
        } else if (note === RANDOM_PAD) {
            setParam("randomize", 1);
            announce("New phrase");
            pollStatus();
        } else if (note >= PRESET_FIRST && note < PRESET_FIRST + PRESETS) {
            onPreset(note - PRESET_FIRST);
        } else if (note <= GATE_LAST) {
            const step = note - PAD_FIRST;
            if (step < Math.min(st.len, 16)) {
                setParam("gate_toggle", step);
                /* Answer the press now; the poll confirms it. */
                st.gates = st.gates.slice(0, step) + (st.gates[step] === "1" ? "0" : "1") + st.gates.slice(step + 1);
            }
        }
        sendLed(note, padColor(note));
    }

    function run(todo) {
        if (todo && todo.action === "menu" && todo.entry && todo.entry.action && runAction)
            return !!runAction(todo.entry.action);
        return false;
    }

    function onMidi(data) {
        const c = st.controller;
        if (!c || !data || data.length < 3 || !displayOn()) return;
        const status = data[0] & 0xf0;
        if ((status === 0x90 || status === 0x80) && data[1] >= PAD_FIRST && data[1] <= PAD_LAST) {
            onPad(data[1], status === 0x90 && data[2] > 0);
            return;
        }
        if (status === 0xb0 && data[1] === 49) { st.shiftCc = data[2] > 0; syncShift(); return; }
        syncShift();                      /* read at the touch itself, not a tick later */
        const intent = decodeInput(data, { shift: st.shift });
        if (!intent) return;
        if (intent.type === "touch") touch(intent.slot, intent.down);
        run(applyInput(c, intent, { nowMs: now(), reveal: st.shift }));
    }

    /* Back peels one layer (peek, picker, entered menu) before it leaves. */
    function handleBack() {
        const c = st.controller;
        if (!c) return false;
        const todo = applyInput(c, { type: "back" }, { nowMs: now() });
        return !(todo && todo.action === "exit");
    }

    /* Leaving: no knob left recording, and the pads (and their colours) go
     * back to Move. */
    function releaseAll() {
        for (const key of Object.values(st.slotKey)) setParam("touch", `${key},0`);
        st.slotKey = {};
        st.heldPads.clear();
        restorePads();
        padBlock(false);
        st.visible = false;
    }

    return { init, tick, onMidi, handleBack, releaseAll, title, padColor, visible, state: st };
}
