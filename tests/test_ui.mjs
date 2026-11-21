/* LUXOR UI test: ui_core.mjs driving Schwung's REAL page controller and
 * input decoder, drawn through the device-font harness, against a fake param
 * store serving the DSP's real contract (grid_hierarchy is dumped by smoke.py
 * from the built dsp.so). Run by scripts/test.sh.
 *
 * Needs a Schwung checkout for the page engine and the harness:
 * SCHWUNG_ROOT, else ../schwung beside this repository. scripts/test.sh
 * skips this test when neither exists. */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = process.env.SCHWUNG_ROOT || path.resolve(here, "../../schwung");
const load = (p) => import(pathToFileURL(p).href);
/* BK_SHARED points at another copy of shared/ -- e.g. one mirrored from the
 * device, whose page engine can differ from this checkout's. */
const shared = process.env.BK_SHARED || path.join(root, "src/shared");
const { createController } = await load(path.join(shared, "param_pages/page_controller.mjs"));
const { decodeInput, applyInput } = await load(path.join(shared, "param_pages/page_input.mjs"));
const { createFramebuffer, drawContext } = await load(path.join(root, "tools/param-pages/harness.mjs"));
const { createUi, MAIN_KEYS, STATUS_EVERY_TICKS, HEARTBEAT_MS, LED,
        PRESET_FIRST, PRESETS, PRESET_PATH, parsePresets } =
    await load(path.join(here, "../ui_core.mjs"));

let failures = 0;
function check(cond, msg) {
    if (!cond) { failures++; console.error("FAIL:", msg); }
}

const build = path.join(here, "../build/native");
const manifest = JSON.parse(fs.readFileSync(path.join(here, "../module.json"), "utf8"));
const chainParams = manifest.capabilities.chain_params;
const gridHierarchy = fs.readFileSync(path.join(build, "grid_hierarchy.json"), "utf8");

/* ---- fake DSP ---------------------------------------------------------- */
const PHRYGIAN = ["C", "Db", "Eb", "F", "G", "Ab", "Bb"];
const CHROMATIC = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"];
const namesFor = (set) => {
    const out = [];
    for (let oct = 1; oct <= 4; oct++) for (const n of set) out.push(n + oct);
    return out.concat(["C5"]).join(",");
};
let pitchNames = namesFor(PHRYGIAN);
const values = {};
for (const p of chainParams) values[p.key] = String(p.default);
const gates = new Array(16).fill(true);
const writes = [];
const reads = [];
let status = "0,-1,0,16,0";
let hwShift = false, shown = true, padBlocked = false;
const leds = {};
const files = new Map();
let stateBlob = '{"v":4,"p":[1]}';
const store = {
    get(key) {
        reads.push(key);
        if (key === "ui_hierarchy") return "";
        if (key === "grid_hierarchy") return gridHierarchy;
        if (key === "chain_params") return JSON.stringify(chainParams);
        if (key === "motion_status") return status;
        if (key === "pitch_names") return pitchNames;
        if (key === "state") return stateBlob;
        if (key === "gate_status") return gates.map((g) => g ? "1" : "0").join("");
        if (key.startsWith("eff_")) return values[key.slice(4)] ?? null;
        if (key in values) return values[key];
        return "";
    },
    set(key, value) {
        writes.push([key, String(value)]);
        if (key === "gate_toggle") gates[Number(value)] = !gates[Number(value)];
        if (key in values) values[key] = String(value);
        return true;
    },
};

let clock = 1000;
const fb = createFramebuffer();
const ctx = drawContext(fb);
const ui = createUi({
    createController, decodeInput, applyInput,
    getParam: (k) => store.get(k),
    setParam: (k, v) => store.set(k, v),
    draw: ctx,
    clear: () => fb.clearScreen(),
    layout: () => "movy",
    now: () => clock,
    /* The device's shift: shared memory, never a CC (shadow_get_shift_held). */
    shiftHeld: () => hwShift,
    displayOn: () => shown,
    padBlock: (on) => { padBlocked = on; },
    sendLed: (n, col) => { leds[n] = col; return true; },
    padSnapshot: () => ({ "68": 5, "99": 7 }),
    readFile: (p) => files.get(p) ?? null,
    writeFile: (p, t) => { files.set(p, t); return true; },
    ensureDir: () => true,
});

function ticks(n) { for (let i = 0; i < n; i++) { clock += 17; ui.tick(); } }
const touchesSent = () => writes.filter(([k]) => k === "touch").map(([, v]) => v);
const bare = (k) => k.slice(k.indexOf(":") + 1);

ui.init();
ticks(40);
const c = ui.state.controller;
check(JSON.stringify(c.page.keys.map(bare)) === JSON.stringify(MAIN_KEYS),
      "page 1 = VOLUME DECAY P.ENV FM AMT MOD DEC CUTOFF DRIVE PITCH: " + c.page.keys);
const PAGES = c.pages.length;
check(PAGES >= 7, `Main / Osc / FM / Filter / FX / LFO / Scale: ${PAGES} pages`);
check(!reads.includes("ui_hierarchy"), "the grid must read grid_hierarchy, never the empty ui_hierarchy");
check(fb.countLit() > 200, "page 1 draws");
console.log(fb.toBlocks ? fb.toBlocks() : fb.toAscii());
check(ui.title() === "LUXOR", ui.title());

/* ---- PITCH is a note of the scale: the DSP's names, by index ------------- */
check(ui.state.names && ui.state.names.length === 29 && ui.state.names[7] === "C2", "names from the DSP");

/* ---- playing: step in the header; no REC until the REC pad ---------------- */
status = "0,4,0,16,0";
ticks(STATUS_EVERY_TICKS);
check(ui.title() === "LUXOR 5/16", "step in header: " + ui.title());
ui.onMidi([0x90, 4, 127]);                          /* knob 5 touch */
check(touchesSent().at(-1) === "cutoff,1", "touch reported: " + touchesSent());
check(ui.title() === "LUXOR 5/16", "no REC without the REC pad: " + ui.title());
ui.onMidi([0x80, 4, 0]);
check(touchesSent().at(-1) === "cutoff,0", "release reported");

/* ---- the pads ----------------------------------------------------------- */
check(padBlocked === true, "pads taken while the screen is up");
ticks(8);                                           /* a full LED sweep */
check(leds[99] === LED.RECORD_OFF, "REC off = dim: " + leds[99]);
check(leds[98] === LED.RANDOM, "RANDOM lit");
check(leds[68] === LED.GATE_ON && leds[72] === LED.PLAYHEAD, "gates lit, playhead on step 5");
check(leds[83] === LED.GATE_ON, "16-step loop: both rows lit");
ui.onMidi([0x90, 99, 100]);                         /* REC pad */
check(writes.at(-1)[0] === "write" && writes.at(-1)[1] === "1" && leds[99] === LED.RECORD_ON, "REC on = solid red");
ui.onMidi([0x90, 99, 100]);                         /* still held: no repeat */
check(writes.filter(([k]) => k === "write").length === 1, "held pad fires once");
ui.onMidi([0x80, 99, 0]);
status = "0,4,0,16,1";
ticks(STATUS_EVERY_TICKS);
check(ui.title() === "LUXOR ARM 5/16", "armed: " + ui.title());
ui.onMidi([0x90, 4, 127]);
check(ui.title() === "LUXOR REC 5/16", "REC under the finger at once: " + ui.title());
ui.onMidi([0xb0, 75, 1]);
ticks(10);
check(writes.some(([k]) => k === "cutoff"), "the turn still writes the knob");
const n0 = touchesSent().filter((t) => t === "cutoff,1").length;
ticks(Math.ceil(HEARTBEAT_MS * 4 / 17));
const beats = touchesSent().filter((t) => t === "cutoff,1").length - n0;
check(beats >= 3 && beats <= 5, `heartbeat restates the touch (${beats} in ${HEARTBEAT_MS * 4} ms)`);
ui.onMidi([0x80, 4, 0]);
check(ui.title() === "LUXOR ARM 5/16", "REC gone on release: " + ui.title());
const quiet = touchesSent().length;
ticks(40);
check(touchesSent().length === quiet, "no heartbeat once released");

ui.onMidi([0x90, 98, 100]);                          /* RANDOM */
check(writes.at(-1)[0] === "randomize" || writes.some(([k]) => k === "randomize"), "RANDOM pad writes a phrase");
check(leds[98] === LED.PRESSED, "RANDOM lights while pressed");
ui.onMidi([0x80, 98, 0]);
check(leds[98] === LED.RANDOM, "...and back");
ui.onMidi([0x90, 69, 100]);                          /* step 2 */
check(writes.at(-1)[0] === "gate_toggle" && writes.at(-1)[1] === "1", "step 2 toggled");
check(leds[69] === LED.GATE_OFF, "step 2 now off: " + leds[69]);
ui.onMidi([0x80, 69, 0]);
ticks(STATUS_EVERY_TICKS);
check(ui.state.gates[1] === "0", "the poll agrees: " + ui.state.gates);
const w0 = writes.length;
ui.onMidi([0x90, 97, 100]); ui.onMidi([0x80, 97, 0]);   /* an unassigned pad */
check(writes.length === w0, "unassigned pads do nothing");
/* a 1/2-bar loop darkens the second row; a 2-bar loop repeats the 16 per bar */
status = "0,3,0,8,1";
ticks(STATUS_EVERY_TICKS + 8);
check(leds[76] === LED.OFF && leds[83] === LED.OFF, "steps 9-16 dark in a 1/2-bar loop");
ui.onMidi([0x90, 80, 100]); ui.onMidi([0x80, 80, 0]);
check(writes.at(-1)[0] !== "gate_toggle" || writes.at(-1)[1] !== "12", "a dark step pad does nothing");
status = "0,20,0,32,1";
ticks(STATUS_EVERY_TICKS + 8);
check(leds[72] === LED.PLAYHEAD && leds[83] !== LED.OFF, "step 21 of 32 lights pad 5: " + leds[72]);

/* ---- PITCH: note in the header while held -------------------------------- */
status = "0,4,0,16,1";
ticks(STATUS_EVERY_TICKS);
c.state.values.pitch = "9";
ui.onMidi([0x90, 7, 127]);                          /* knob 8 = pitch */
check(touchesSent().at(-1) === "pitch,1", "knob 8 is pitch");
check(ui.title() === "PITCH Eb2 REC 5/16", "pitch note while held: " + ui.title());
ui.onMidi([0x90, 7, 0]);                            /* velocity-0 release spelling */
check(touchesSent().at(-1) === "pitch,0", "note-on vel 0 releases");

/* ---- an automated knob is drawn as modulated, from eff_<key> -------------- */
status = `${1 << 4},3,0,16,1`;                     /* lane 4 = cutoff */
reads.length = 0;
ticks(STATUS_EVERY_TICKS * 30);
check(c.isModulatedCached("cutoff"), "automated knob marked modulated");
check(reads.includes("eff_cutoff"), "its playing value is read as eff_cutoff");

/* ---- Shift + touch clears one lane; on VOLUME, all -------------------------
 * Shift as the DEVICE delivers it: no CC 49 at all, only shared memory. */
const before = touchesSent().length;
hwShift = true;
ui.onMidi([0x90, 4, 127]);
check(writes.at(-1)[0] === "motion_clear" && writes.at(-1)[1] === "cutoff", "shift+touch clears the lane: " + writes.at(-1));
ui.onMidi([0x80, 4, 0]);
ui.onMidi([0x90, 0, 127]);
check(writes.at(-1)[0] === "motion_clear" && writes.at(-1)[1] === "all", "shift+touch VOLUME clears all");
ui.onMidi([0x80, 0, 0]);
hwShift = false;
ticks(1);
check(touchesSent().length === before, "shift+touch never starts a recording");
ui.onMidi([0x90, 0, 127]); ui.onMidi([0x80, 0, 0]);
check(touchesSent().length === before, "VOLUME touch is never a recording");

/* ---- page 2 (Osc): the LXR's oscillator page, not recorded ---------------- */
const jog = (d) => { ui.onMidi([0xb0, 14, d > 0 ? 1 : 127]); ticks(20); };
jog(1);
check(JSON.stringify(c.page.keys.map(bare)) ===
      JSON.stringify(["wave", "pwm", "attack", "a_slope", "p_slope"]), "page 2 = Osc: " + c.page.keys);
const n2 = touchesSent().length;
ui.onMidi([0x90, 0, 127]); ui.onMidi([0x80, 0, 0]);
check(touchesSent().length === n2, "TUNE is not recorded");
jog(1);
check(bare(c.page.keys[0]) === "fm_freq", "page 3 = FM: " + c.page.keys);
jog(1);
check(bare(c.page.keys[0]) === "reso", "page 4 = Filter: " + c.page.keys);

/* ---- page 5 (FX): MIX, TYPE, then only the selected FX's own knobs -------- */
jog(1);
const fxKeys = () => c.page.keys.filter(Boolean).map(bare);
check(JSON.stringify(fxKeys()) === JSON.stringify(["fx_mix", "fx_type"]), "FX Off: MIX + TYPE only: " + fxKeys());
const want = {
    1: ["drv_type", "drv_drive", "drv_color", "drv_tone", "drv_vol"],
    2: ["ring_wave", "ring_freq"],
    3: ["cmp_ratio", "cmp_attack", "cmp_decay", "cmp_thresh", "cmp_gain"],
    4: ["dly_time", "dly_range", "dly_tone", "dly_fdbk"],
    5: ["pp_time", "pp_range", "pp_tone", "pp_fdbk"],
};
for (let t = 1; t <= 5; t++) {
    /* knob 2: FX TYPE up to the next type (an enum moves after a few detents) */
    for (let d = 0; d < 40 && values.fx_type !== String(t); d++) { ui.onMidi([0xb0, 72, 1]); ticks(3); }
    ticks(30);
    check(values.fx_type === String(t), `FX TYPE written: ${values.fx_type}`);
    check(JSON.stringify(fxKeys()) === JSON.stringify(["fx_mix", "fx_type", ...want[t]]),
          `FX ${t}: knobs 3.. are that FX's: ${fxKeys()}`);
    check(c.pages.length === PAGES, `the FX page never spills onto a second page: ${c.pages.length}`);
}
console.log(fb.toBlocks ? fb.toBlocks() : fb.toAscii());
const n3 = touchesSent().length;
ui.onMidi([0x90, 2, 127]); ui.onMidi([0x80, 2, 0]);
check(touchesSent().length === n3, "FX knobs are not recorded (yet)");
check(ui.visible({ param: "fx_type", equals: Number(values.fx_type) }) &&
      !ui.visible({ param: "fx_type", equals: 1 }), "the evaluator: " + values.fx_type);
check(ui.visible(null) && ui.visible({ param: "nope", equals: 1 }), "an unreadable condition is visible");

/* ---- page 6 (LFO) ------------------------------------------------------- */
jog(1);
check(bare(c.page.keys[0]) === "lfo_rate" && bare(c.page.keys[6]) === "lfo_dest", "page 6 = LFO: " + c.page.keys);

/* ---- page 7: ROOT SCALE LENGTH TUNE FINE; a scale change re-reads names ---- */
jog(1);
check(JSON.stringify(c.page.keys.filter(Boolean).map(bare)) ===
      JSON.stringify(["root", "scale", "length", "tune", "fine"]), "page 7 = ROOT SCALE LENGTH TUNE FINE: " + c.page.keys);
pitchNames = namesFor(CHROMATIC);
ui.onMidi([0xb0, 72, 1]);                            /* turn SCALE */
ticks(30);
check(writes.some(([k]) => k === "scale"), "SCALE written");
check(ui.state.names.length === 49, "names re-read after a scale change: " + (ui.state.names && ui.state.names.length));
console.log(fb.toBlocks ? fb.toBlocks() : fb.toAscii());

/* ---- a finger held across a page change is released on the lane it held -- */
for (let i = 0; i < 6; i++) jog(-1);                 /* page 1 */
check(bare(c.page.keys[6]) === "decay", "back on page 1: " + c.page.keys);
ui.onMidi([0x90, 6, 127]);                           /* hold DECAY */
ui.onMidi([0xb0, 14, 1]);                            /* jog while held */
ticks(20);
ui.onMidi([0x80, 6, 0]);
check(touchesSent().at(-1) === "decay,0", "release goes to the lane that was touched: " + touchesSent().at(-1));
ui.onMidi([0xb0, 14, 127]); ticks(20);

/* ---- the screen closes: pads, colours and touches go back ---------------- */
ui.onMidi([0x90, 1, 127]);                           /* hold P.ENV */
shown = false;
ticks(1);
check(padBlocked === false, "pads handed back");
check(leds[68] === 5 && leds[99] === 7 && leds[80] === 0, "Move's pad colours restored");
check(touchesSent().at(-1) === "p_env,0", "a held knob is released on leave");
shown = true;
ticks(1);
check(padBlocked === true, "taken again when shown");

/* ---- the preset pads (second row, 84-91) ---------------------------------- */
check(PRESET_FIRST === 84 && PRESETS === 8, "presets are pads 84-91");
check(leds[86] === LED.OFF, "an empty preset is dark");
let mark = writes.length;
ui.onMidi([0x90, 86, 127]); ui.onMidi([0x80, 86, 0]);
check(!writes.slice(mark).some(([k]) => k === "state"), "an empty preset loads nothing");
check(/EMPTY 3/.test(ui.title()), "an empty preset says so: " + ui.title());
hwShift = true; ticks(1);
ui.onMidi([0x90, 86, 127]); ui.onMidi([0x80, 86, 0]);
hwShift = false; ticks(1);
check(parsePresets(files.get(PRESET_PATH))[2] === stateBlob, "Shift+pad stores the state");
check(/SAVED PRESET 3/.test(ui.title()), "save confirmed: " + ui.title());
check(leds[86] === LED.PRESET_CURRENT, "the saved preset is lit white");
const saved = stateBlob;
stateBlob = "other";
mark = writes.length;
ui.onMidi([0x90, 86, 127]); ui.onMidi([0x80, 86, 0]);
check(writes.slice(mark).some(([k, v]) => k === "state" && v === saved), "a tap recalls the saved state");
clock += 2000; ticks(1);

/* ---- Back at the grid leaves ---------------------------------------------- */
ui.onMidi([0x90, 6, 127]);
check(ui.handleBack() === false, "Back at the grid exits");
ui.releaseAll();
check(touchesSent().at(-1) === "decay,0" && padBlocked === false, "leaving releases knob + pads");

/* a failed status read keeps the last answer */
status = null;
ticks(STATUS_EVERY_TICKS);
check(ui.state.step === 3, "null status read changes nothing");

check(fb.clipped() === 0, `no pixels off screen (${fb.clipped()})`);
if (failures) { console.error(`${failures} UI check(s) failed`); process.exit(1); }
console.log("PASS: UI drives the page engine, pads (REC/RANDOM/16 gates), touch + heartbeat, shift+touch clear, scale-index PITCH names, pages, FX page per type");
