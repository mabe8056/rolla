#!/usr/bin/env python3
"""Render rolls against a generated sample pack and assert what a real pack must give.

The pack is synthesised at test time (sine, chord and noise one-shots), so no sample
audio is stored in the repo. It holds more files than the old 384-slot load table,
which is how lanes used to go silent after the first few rolls of a session.

Checks, for every roll of every seed and style:
  * the bar-1 downbeat kick sounds (DRUMS stem, first 10 ms) when the kick is at sample 0
  * a lane whose MIDI has notes and that has a picked file is not a silent stem
  * a muted lane (hitsMute) is silent in its stem
  * nothing is written into the sample folder
  * the full mix stays under -6 dBFS sample peak
Optionally (--stab-sweep) renders STAB 0 and 1 per style and asserts the stab MIDI changes.
"""
import argparse
import json
import math
import os
import random
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "common"))
from scratch_env import scratch_env  # noqa: E402

SR = 48000
NOTE_NAMES = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"]


def write_wav(path, frames, sr=SR):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        data = bytearray()
        for x in frames:
            v = int(max(-1.0, min(1.0, x)) * 32000)
            data += struct.pack("<h", v)
        w.writeframes(bytes(data))


def faded(samples, fade_in=0.001, fade_out=0.01):
    n = len(samples)
    a = max(1, int(fade_in * SR))
    b = max(1, int(fade_out * SR))
    out = list(samples)
    for i in range(min(a, n)):
        out[i] *= i / a
    for i in range(min(b, n)):
        out[n - 1 - i] *= i / b
    return out


def tone(freqs, secs, amp=0.5, decay=4.0):
    n = int(secs * SR)
    out = []
    for i in range(n):
        t = i / SR
        e = math.exp(-decay * t)
        out.append(amp * e * sum(math.sin(2 * math.pi * f * t) for f in freqs) / len(freqs))
    return faded(out)


def kick(rng):
    n = int(0.35 * SR)
    f0 = rng.uniform(45, 60)
    out, ph = [], 0.0
    for i in range(n):
        t = i / SR
        f = f0 + 120 * math.exp(-t * 30)
        ph += 2 * math.pi * f / SR
        out.append(0.8 * math.exp(-t * 9) * math.sin(ph))
    return faded(out, 0.0005, 0.02)


def noise(rng, secs, amp=0.4, decay=30.0):
    n = int(secs * SR)
    return faded([amp * math.exp(-decay * i / SR) * rng.uniform(-1, 1) for i in range(n)])


def build_pack(root, per_lane, per_voice, seed=7):
    rng = random.Random(seed)
    count = 0
    for lane, secs in (("BASS", 0.8), ("STAB", 0.6), ("HITS", 0.4), ("PLUCK", 0.4), ("PAD", 2.0), ("VOX", 0.8)):
        for k in range(per_lane):
            pc = k % 12
            f = 65.41 * 2 ** (pc / 12) * (1 if lane == "BASS" else 4)
            freqs = [f] if lane in ("BASS", "PLUCK", "VOX") else [f, f * 1.189, f * 1.498]
            write_wav(root / lane / f"SYN_{lane.title()}_{k:02d}_{NOTE_NAMES[pc]}.wav",
                      tone(freqs, secs, decay=1.0 if lane == "PAD" else 4.0))
            count += 1
    for voice in ("KICK", "SNARE", "CLAP", "RIM", "CH", "OH", "RIDE", "PERC"):
        for k in range(per_voice):
            if voice == "KICK":
                frames = kick(rng)
            elif voice in ("CH", "RIM", "PERC"):
                frames = noise(rng, 0.08)
            else:
                frames = noise(rng, 0.3, decay=12.0)
            write_wav(root / "DRUMS" / voice / f"SYN_{voice.title()}_{k:02d}.wav", frames)
            count += 1
    return count


def read_wav(path):
    with wave.open(str(path), "rb") as w:
        ch, width, n = w.getnchannels(), w.getsampwidth(), w.getnframes()
        raw = w.readframes(n)
    if width == 3:
        vals = [int.from_bytes(raw[i:i + 3], "little", signed=True) / 8388608.0 for i in range(0, len(raw), 3)]
    elif width == 2:
        vals = [v / 32768.0 for v in struct.unpack("<%dh" % (len(raw) // 2), raw)]
    elif width == 4:
        vals = list(struct.unpack("<%df" % (len(raw) // 4), raw))
    else:
        raise SystemExit(f"unsupported wav width {width} in {path}")
    return vals, ch


def peak(vals, start=0, end=None):
    seg = vals[start:end]
    return max((abs(v) for v in seg), default=0.0)


def db(x):
    return 20 * math.log10(x) if x > 0 else -240.0


def midi_note_count(path):
    data = Path(path).read_bytes()
    i, notes = 14, 0
    while i + 8 <= len(data):
        if data[i:i + 4] != b"MTrk":
            break
        size = struct.unpack(">I", data[i + 4:i + 8])[0]
        j, end, status = i + 8, i + 8 + size, 0
        while j < end:
            while data[j] & 0x80:
                j += 1
            j += 1
            b = data[j]
            if b == 0xFF:
                ln, k = 0, j + 2
                while True:
                    ln = (ln << 7) | (data[k] & 0x7F)
                    k += 1
                    if not data[k - 1] & 0x80:
                        break
                j = k + ln
                continue
            if b in (0xF0, 0xF7):
                ln, k = 0, j + 1
                while True:
                    ln = (ln << 7) | (data[k] & 0x7F)
                    k += 1
                    if not data[k - 1] & 0x80:
                        break
                j = k + ln
                continue
            if b & 0x80:
                status = b
                j += 1
            kind = status & 0xF0
            if kind in (0xC0, 0xD0):
                j += 1
            else:
                if kind == 0x90 and data[j + 1] > 0:
                    notes += 1
                j += 2
        i = end
    return notes


LANES = {"bass": "BASS", "stab": "STAB", "hits": "HITS", "pluck": "PLUCK", "pad": "PAD", "vox": "VOX", "drums": "DRUMS"}


def check_roll(roll, fails):
    sidecars = [p for p in roll.glob("ROLLA_*.json")]
    if not sidecars:
        fails.append(f"{roll}: no sidecar (silent render was skipped)")
        return
    side = json.loads(sidecars[0].read_text())
    mix = sidecars[0].with_suffix(".wav")
    stem_of = lambda lane: mix.with_name(mix.stem + "_" + LANES[lane] + ".wav")
    vals, ch = read_wav(mix)
    mp = peak(vals)
    if db(mp) > -6.0:
        fails.append(f"{roll.name} {side['style']}: mix peak {db(mp):.1f} dBFS over -6")
    files = side.get("files", {})
    if side.get("kickMidiSample", -1) == 0 and files.get("kick"):
        dv, dch = read_wav(stem_of("drums"))
        first = peak(dv, 0, int(0.010 * SR) * dch)
        if db(first) < -40.0:
            fails.append(f"{roll.name} {side['style']}: bar-1 kick silent ({db(first):.1f} dBFS in the first 10 ms)")
    for lane in ("bass", "stab", "hits", "pluck", "pad", "vox"):
        mids = list(roll.glob(f"*_{lane}.mid"))
        notes = midi_note_count(mids[0]) if mids else 0
        stem = stem_of(lane)
        if not stem.exists():
            continue
        sp = db(peak(read_wav(stem)[0]))
        muted = lane == "hits" and side.get("hitsMute")
        if muted:
            if sp > -90.0:
                fails.append(f"{roll.name} {side['style']}: muted {lane} sounds ({sp:.1f} dBFS)")
            continue
        if notes > 0 and files.get(lane) and sp < -60.0:
            fails.append(f"{roll.name} {side['style']}: {lane} has {notes} notes but the stem is silent ({sp:.1f} dBFS)")


def stab_notes(rolls, style, seed, value, out, cache):
    d = out / f"stab_{style}_{value}"
    run(rolls, ["--style", style, "--count", "6", "--seed", str(seed), "--out", str(d),
                "--knob", f"STAB={value}", "--knob", "BUSY=0.40"], cache)
    total = 0
    for mid in sorted(d.glob("*/roll_*/*_stab.mid")):
        total += midi_note_count(mid)
    return total


def profile_env(cache):
    # Scratch profile rooted at the work folder (the cache's parent): never the real one.
    return scratch_env(Path(cache).parent, cache=cache)


def run(rolls, args, cache):
    env = profile_env(cache)
    res = subprocess.run([rolls] + args, env=env, capture_output=True, text=True)
    if res.returncode != 0:
        sys.stdout.write(res.stdout[-4000:])
        sys.stderr.write(res.stderr[-4000:])
        raise SystemExit(f"rolla-rolls failed ({res.returncode}): {' '.join(args)}")
    for line in res.stderr.splitlines():
        if line.startswith("SAMPLE NOT READY") or "load table full" in line:
            print("WARN", line)
    return res


def render_set(rolls, pack, out, cache, seed, count, stem_lanes, jobs):
    """One rolla-rolls per style; up to `jobs` at a time."""
    styles = ["ukmin", "long", "pop", "jackin"]
    env = profile_env(cache)
    pending, running, logs = list(styles), [], {}
    while pending or running:
        while pending and len(running) < max(1, jobs):
            st = pending.pop(0)
            args = [rolls, "--samples", str(pack), "--style", st, "--count", str(count), "--seed", str(seed),
                    "--out", str(out), "--stem-lanes", stem_lanes, "--knob", "BUSY=0.40"]
            out.mkdir(parents=True, exist_ok=True)
            log = open(out.parent / f"{out.name}_{st}.log", "w")
            running.append((st, subprocess.Popen(args, env=env, stdout=log, stderr=subprocess.STDOUT), log))
        for item in list(running):
            st, proc, log = item
            if proc.poll() is not None:
                log.close()
                running.remove(item)
                text = Path(log.name).read_text(errors="replace")
                for line in text.splitlines():
                    if line.startswith("SAMPLE NOT READY") or "load table full" in line:
                        print("WARN", st, line)
                if proc.returncode != 0:
                    print(text[-3000:])
                    raise SystemExit(f"rolla-rolls failed for {st} seed {seed} ({proc.returncode})")
        if running:
            import time
            time.sleep(0.5)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rolls", required=True)
    ap.add_argument("--seeds", default="1,2", help="seeds for the bar-1 kick check (DRUMS stem)")
    ap.add_argument("--count", type=int, default=20)
    ap.add_argument("--full-count", type=int, default=6,
                    help="rolls per style of the first seed rendered with every stem (silent-lane and mute checks)")
    ap.add_argument("--per-lane", type=int, default=70)
    ap.add_argument("--per-voice", type=int, default=50)
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--stab-sweep", action="store_true")
    ap.add_argument("--work", default="")
    ap.add_argument("--part", choices=("all", "full", "kick", "stab"), default="all",
                    help="run one block so CI can spread them over parallel jobs: full (every stem, first seed), "
                         "kick (bar-1 kick per seed), stab (--stab-sweep). all runs every block, as before")
    a = ap.parse_args()

    work = Path(a.work) if a.work else Path(tempfile.mkdtemp(prefix="rolla-pack-"))
    pack, cache, out = work / "Samples", work / "cache", work / "out"
    cache.mkdir(parents=True, exist_ok=True)
    if not pack.exists():
        n = build_pack(pack, a.per_lane, a.per_voice)
        print(f"pack: {n} generated files in {pack}")
    before = {p.relative_to(pack) for p in pack.rglob("*") if p.is_file()}

    fails = []
    seeds = [int(s) for s in a.seeds.split(",") if s]
    if a.part in ("all", "full") and a.full_count > 0 and seeds:
        d = out / f"full_seed_{seeds[0]}"
        render_set(a.rolls, pack, d, cache, seeds[0], a.full_count, "BASS,STAB,HITS,PLUCK,PAD,VOX,DRUMS", a.jobs)
        rolls = sorted(d.glob("*/roll_*"))
        print(f"all stems, seed {seeds[0]}: {len(rolls)} rolls")
        for roll in rolls:
            check_roll(roll, fails)
    for seed in (seeds if a.part in ("all", "kick") else []):
        d = out / f"seed_{seed}"
        render_set(a.rolls, pack, d, cache, seed, a.count, "DRUMS", a.jobs)
        rolls = sorted(d.glob("*/roll_*"))
        print(f"bar-1 kick, seed {seed}: {len(rolls)} rolls")
        if len(rolls) != 4 * a.count:
            fails.append(f"seed {seed}: {len(rolls)} roll folders, want {4 * a.count}")
        for roll in rolls:
            check_roll(roll, fails)

    after = {p.relative_to(pack) for p in pack.rglob("*") if p.is_file()}
    extra = sorted(str(p) for p in after - before)
    if extra:
        fails.append(f"files written into the sample folder: {extra[:5]}")

    if a.stab_sweep and a.part in ("all", "stab"):
        for style in ("ukmin", "long", "pop", "jackin"):
            lo = stab_notes(a.rolls, style, 7, 0.0, out, cache)
            mid = stab_notes(a.rolls, style, 7, 0.55, out, cache)
            hi = stab_notes(a.rolls, style, 7, 1.0, out, cache)
            print(f"STAB sweep {style}: stab notes {lo} / {mid} / {hi} at 0 / 0.55 / 1")
            if not (lo < mid < hi):
                fails.append(f"STAB does not move {style} stab density ({lo} / {mid} / {hi})")

    for f in fails:
        print("FAIL", f)
    print(f"{'FAIL' if fails else 'PASS'}: {len(fails)} problems", flush=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
