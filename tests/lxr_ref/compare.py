#!/usr/bin/env python3
"""Run every scene through ref_main and port_main; compare the int16 streams.

Prints per scene: identical?, peak / RMS (dBFS) of the reference, and on a
difference the first differing frame and the largest error. Exit 1 if any
scene differs. Also writes ref_<n>.wav / port_<n>.wav for listening."""
import array, math, pathlib, subprocess, sys, wave

out = pathlib.Path(sys.argv[1])
sys.path.insert(0, str(pathlib.Path(__file__).parent))
COUNT = 24
bad = 0

def db(x):
    return -math.inf if x <= 0 else 20 * math.log10(x / 32768.0)

def to_wav(raw, path):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
        w.writeframes(raw)

for s in range(COUNT):
    r = out / f"ref_{s}.raw"; p = out / f"port_{s}.raw"
    subprocess.run([str(out / "ref_main"), str(s), str(r)], check=True)
    subprocess.run([str(out / "port_main"), str(s), str(p)], check=True)
    a = array.array("h", r.read_bytes()); b = array.array("h", p.read_bytes())
    peak = max(abs(x) for x in a); rms = math.sqrt(sum(x * x for x in a) / len(a))
    if a == b:
        print(f"scene {s:2d}: identical  ({len(a)//2} frames, peak {db(peak):6.1f} dBFS, rms {db(rms):6.1f} dBFS)")
    else:
        bad += 1
        first = next(i for i in range(min(len(a), len(b))) if a[i] != b[i])
        err = max(abs(x - y) for x, y in zip(a, b))
        print(f"scene {s:2d}: DIFFERS from frame {first//2} (max |err| {err}), peak {db(peak):6.1f} dBFS")
    to_wav(r.read_bytes(), out / f"ref_{s}.wav"); to_wav(p.read_bytes(), out / f"port_{s}.wav")
    r.unlink(); p.unlink()
print("ALL IDENTICAL" if not bad else f"{bad} scene(s) differ")
sys.exit(1 if bad else 0)
