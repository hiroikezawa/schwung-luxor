#!/usr/bin/env python3
"""Embed module.json's chain_params in the DSP so get_param never reads a file.

Also cross-checks the manifest against the engine's own parameter table, so the
knob metadata the host shows can never drift from what the DSP does.
"""
import json
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
params = json.loads((root / "module.json").read_text())["capabilities"]["chain_params"]

engine = (root / "dsp" / "bk_engine.c").read_text()
table = engine.split("bk_params[BP_COUNT] = {", 1)[1].split("};", 1)[0]
rows = re.findall(r'\{"([a-z0-9_]+)",\s*([-0-9.]+)f,\s*([-0-9.]+)f,\s*([-0-9.]+)f,\s*([01])\}', table)
assert [p["key"] for p in params] == [r[0] for r in rows], ("chain_params order != bk_params", rows)
for p, (key, lo, hi, d, is_int) in zip(params, rows):
    assert float(p["default"]) == float(d), (key, "default drift")
    if key == "pitch":
        # The engine clamps to 48 (a chromatic scale's 49 notes); module.json
        # and the served chain_params carry the CURRENT scale's top index
        # (28 = C Phrygian's, the default).
        assert p["type"] == "int" and is_int == "1" and p["min"] == 0 and p["max"] == 28, p
        continue
    if p["type"] == "enum":
        assert is_int == "1" and float(lo) == 0 and float(hi) == len(p["options"]) - 1, key
    else:
        assert is_int == ("1" if p["type"] == "int" else "0"), (key, "int/float drift")
        assert float(p["min"]) == float(lo) and float(p["max"]) == float(hi), key

# Split around PITCH's max so the plugin can serve the range of the scale in
# use: head + "%d" + tail.
text = json.dumps(params, separators=(",", ":"))
marker = '"key":"pitch","name":"Pitch","short_name":"PITCH","type":"int","min":0,"max":'
assert text.count(marker) == 1 and text[text.index(marker) + len(marker):].startswith("28,")
cut = text.index(marker) + len(marker)
head, tail = text[:cut], text[cut + 2:]
lit = lambda t: json.dumps(t, ensure_ascii=True)
out = pathlib.Path(sys.argv[1])
out.write_text("/* Generated from module.json by scripts/metadata.py. Do not edit. */\n"
               "static const char bk_chain_params_head[] = " + lit(head) + ";\n"
               "static const char bk_chain_params_tail[] = " + lit(tail) + ";\n")
