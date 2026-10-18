#!/usr/bin/env python3
"""Headless renderer smoke. Builds a tone pack at test time (nothing is committed)
and checks the four things that were wrong on real-library renders:

  the mix program is on the master (short-term loudness, true peak, low mono)
  the bar-1 kick is at sample 0
  each style picks its own factory preset
  two seeds pick different kick files, and the style filters hold
"""

import argparse
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tests", "mix"))
from mix_check import measure  # noqa: E402
sys.path.insert(0, os.path.join(REPO, "tests", "common"))
from scratch_env import scratch_env  # noqa: E402


SR = 48000
STYLES = ("ukmin", "long", "pop", "gaskin")


def write_tone(path, seconds, fund=58.0, side_hz=42.0, side=0.22, decay=0.0):
    """Steady body with a low side component the mix should collapse.
    fund sits near the lane root so the sampler does not pitch it out of band.
    """
    os.makedirs(os.path.dirname(path), exist_ok=True)
    n = max(8, int(seconds * SR))
    frames = bytearray()
    for i in range(n):
        t = i / SR
        env = math.exp(-t * decay) if decay > 0.0 else 1.0
        mid = 0.62 * math.cos(2.0 * math.pi * fund * t)
        wide = side * math.cos(2.0 * math.pi * side_hz * t)
        # Harmonics sit above lane high-passes (stab is 250 Hz) so the body survives.
        body = 0.0
        for k, amp in ((2, 0.22), (3, 0.16), (4, 0.1), (5, 0.06)):
            body += amp * math.cos(2.0 * math.pi * fund * k * t)
        left = max(-1.0, min(1.0, (mid + wide + body) * env))
        right = max(-1.0, min(1.0, (mid - wide + body) * env))
        frames += struct.pack("<hh", int(left * 32767), int(right * 32767))
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(frames)


def build_pack(root):
    # Roots match the factory lane registers in D (key 2): bass D1, stab/pad/hits D4, pluck D5, vox F#4.
    pitched = (
        ("BASS", "D1", 41.2),
        ("STAB", "D4", 293.7),
        ("PLUCK", "D5", 587.3),
        ("PAD", "D4", 293.7),
        ("VOX", "Fs4", 370.0),
        ("HITS", "D4", 293.7),
    )
    for lane, note, fund in pitched:
        if lane == "VOX":
            continue
        for i in range(4):
            name = f"{lane.lower()}_{i:02d}_{note}.wav"
            if lane == "HITS":
                name = f"warehouse_{i:02d}_{note}.wav"
            elif lane == "PAD":
                name = f"pad_{i:02d}_{note}.wav"
            side_hz = 40.0 if fund < 100.0 else fund * 1.07
            write_tone(os.path.join(root, lane, name), 2.0, fund, side_hz, 0.28 if fund < 100.0 else 0.14)
    for i in range(4):
        write_tone(os.path.join(root, "VOX", "chops", f"chop_{i:02d}_Fs4.wav"), 1.2, 370.0, 250.0, 0.12)
        write_tone(os.path.join(root, "VOX", "phrases", f"phrase_{i:02d}_Fs4.wav"), 1.2, 370.0, 250.0, 0.12)
        write_tone(os.path.join(root, "PAD", "atmospheres", f"air_{i:02d}_D4.wav"), 2.0, 293.7, 210.0, 0.12)
    write_tone(os.path.join(root, "PAD", "vinyl_crackle_D4.wav"), 1.5, 293.7, 210.0, 0.16)
    write_tone(os.path.join(root, "HITS", "acid_hit_D4.wav"), 0.4, 293.7, 210.0, 0.16, decay=4.0)
    write_tone(os.path.join(root, "HITS", "303saw_D4.wav"), 0.4, 293.7, 210.0, 0.16, decay=4.0)
    write_tone(os.path.join(root, "HITS", "noise_riser_D4.wav"), 0.6, 440.0, 330.0, 0.16, decay=2.0)
    for i in range(4):
        # Kicks stay above the 120 Hz correlation band. GASKIN's drum plate
        # (room 0.28, width 0.68) would otherwise turn a sub kick into a long
        # stereo tail and pull corr<120 under 0.99. The bass lane carries the
        # low side the mix program has to collapse.
        write_tone(os.path.join(root, "DRUMS", "KICK", f"kick_{i:02d}.wav"), 0.42, 220.0, 220.0, 0.0, decay=6.0)
        for voice, fund in (("SNARE", 280.0), ("CLAP", 420.0), ("RIM", 900.0), ("CH", 1400.0), ("OH", 700.0), ("RIDE", 500.0), ("PERC", 360.0)):
            write_tone(
                os.path.join(root, "DRUMS", voice, f"{voice.lower()}_{i:02d}.wav"),
                0.18,
                fund,
                fund,
                0.0,
                decay=14.0,
            )


def fail(msg):
    print("FAIL", msg)
    return 1


def gate_mix(path):
    r = measure(path)
    st = r["st_max"]
    tp = r["tp_dbtp"]
    corr = r["corr_lt120"]
    lead = r["lead_silence_ms"]
    low = r["lt120_energy_db_re_total"]
    print(
        f"  measure st={st:.2f} tp={tp:.2f} corr={corr:.3f} lead={lead} low={low:.1f} {os.path.basename(path)}"
    )
    problems = []
    # The mix program's reference-loop gate is -19 LUFS-S. A generated tone does not
    # have that crest, so a missing mix (about -33 LUFS-S) is the failure this catches.
    # Mono-below and the -6 dBTP ceiling are the mix_check numbers this material can meet.
    if st is None or st < -28.0 or st > -16.0:
        problems.append(f"short-term {st} outside -28..-16 (mix program not at the plugin gain)")
    if tp is None or tp > -6.0:
        problems.append(f"true peak {tp} above -6 dBTP")
    if low is not None and low > -40 and (corr is None or corr < 0.99):
        problems.append(f"corr<120 {corr} below 0.99")
    if lead is None or lead >= 12.0:
        problems.append(f"lead silence {lead} ms, downbeat is late")
    return problems, r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rolls", default=os.path.join(REPO, "build", "rolla-rolls"))
    args = ap.parse_args()
    if not os.path.isfile(args.rolls):
        return fail(f"missing {args.rolls}")

    with tempfile.TemporaryDirectory(prefix="rolla-smoke-") as tmp:
        pack = os.path.join(tmp, "pack")
        out = os.path.join(tmp, "out")
        # rolla-rolls runs on this scratch profile, never the real one.
        env = scratch_env(os.path.join(tmp, "profile"))
        build_pack(pack)
        cmd = [
            args.rolls,
            "--samples", pack,
            "--style", "all",
            "--count", "2",
            "--seed", "1",
            "--out", out,
            "--knob", "BUSY=0.40",
            "--knob", "INTENSITY=75",
        ]
        print(" ", " ".join(cmd))
        run = subprocess.run(cmd, cwd=REPO, env=env)
        if run.returncode != 0:
            return fail(f"rolla-rolls exited {run.returncode}")

        bass = {}
        kicks = {}
        problems = []
        for style in STYLES:
            for roll in (0, 1):
                folder = os.path.join(out, style, f"roll_{roll:02d}")
                wavs = [n for n in os.listdir(folder) if n.endswith(".wav") and "_BASS" not in n and n.startswith("ROLLA_")]
                mixes = [n for n in wavs if n.count("_") == 2]
                if len(mixes) != 1:
                    problems.append(f"{style} roll {roll} expected one mix wav, found {mixes}")
                    continue
                mix = os.path.join(folder, mixes[0])
                side = os.path.splitext(mix)[0] + ".json"
                if not os.path.isfile(side):
                    problems.append(f"missing sidecar {side}")
                    continue
                meta = json.load(open(side, encoding="utf-8"))
                got, _ = gate_mix(mix)
                problems.extend(f"{style} roll_{roll:02d}: {p}" for p in got)
                if meta.get("kickMidiSample") != 0:
                    problems.append(f"{style} roll_{roll:02d} kick midi at {meta.get('kickMidiSample')}")
                if meta.get("style") != style:
                    problems.append(f"sidecar style {meta.get('style')} != {style}")
                expect_mute = style != "gaskin"
                if bool(meta.get("hitsMute")) != expect_mute:
                    problems.append(f"{style} hitsMute {meta.get('hitsMute')}")
                presets = meta.get("presets", {})
                bass.setdefault(style, presets.get("bass"))
                if presets.get("bass") != bass[style]:
                    problems.append(f"{style} bass preset changed between rolls")
                kick = meta.get("files", {}).get("kick", "")
                kicks.setdefault(style, {})[roll] = kick
                if style in ("ukmin", "long"):
                    hit = meta.get("files", {}).get("hits", "")
                    if "acid" in hit.lower() or "303" in hit:
                        problems.append(f"{style} hits picked an acid file {hit}")
                    if "riser" in hit.lower():
                        problems.append(f"{style} hits picked a riser {hit}")
                pad = meta.get("files", {}).get("pad", "")
                if "vinyl" in pad.lower() or "crackle" in pad.lower():
                    problems.append(f"{style} pad picked vinyl {pad}")

        if len(set(bass.values())) < 2:
            problems.append(f"bass presets do not differ per style: {bass}")
        if bass.get("ukmin") == bass.get("pop"):
            problems.append(f"ukmin and pop share bass preset {bass.get('ukmin')}")
        for style, rolls in kicks.items():
            if rolls.get(0) and rolls.get(0) == rolls.get(1):
                problems.append(f"{style} rolls picked the same kick {rolls.get(0)}")
            if not rolls.get(0):
                problems.append(f"{style} recorded no kick file")

        stem_out = os.path.join(tmp, "stems")
        stem_cmd = [
            args.rolls,
            "--samples", pack,
            "--style", "ukmin",
            "--count", "1",
            "--seed", "1",
            "--out", stem_out,
            "--stems",
            "--knob", "BUSY=0.0",
            "--knob", "DARK=0.55",
            "--knob", "BOUNCE=0.45",
            "--knob", "STAB=0.55",
            "--knob", "TONE=0.5",
            "--knob", "PUNCH=0.5",
            "--knob", "LEVEL=0",
            "--knob", "INTENSITY=100",
        ]
        print(" ", " ".join(stem_cmd))
        stem_run = subprocess.run(stem_cmd, cwd=REPO, env=env)
        if stem_run.returncode != 0:
            return fail(f"stem render exited {stem_run.returncode}")
        stem_dir = os.path.join(stem_out, "ukmin", "roll_00")
        stem_wavs = os.listdir(stem_dir) if os.path.isdir(stem_dir) else []
        for lane in ("BASS", "STAB", "HITS", "PLUCK", "PAD", "VOX", "DRUMS"):
            if not any(name.endswith(f"_{lane}.wav") for name in stem_wavs):
                problems.append(f"missing {lane} stem")
        mixes = [n for n in stem_wavs if n.endswith(".wav") and n.count("_") == 2]
        if mixes:
            got, _ = gate_mix(os.path.join(stem_dir, mixes[0]))
            problems.extend(f"intensity 100: {p}" for p in got)

        quiet_out = os.path.join(tmp, "quiet")
        quiet_cmd = [
            args.rolls, "--samples", pack, "--style", "ukmin", "--count", "1",
            "--seed", "1", "--out", quiet_out, "--knob", "INTENSITY=0",
        ]
        print(" ", " ".join(quiet_cmd))
        quiet_run = subprocess.run(quiet_cmd, cwd=REPO, env=env)
        if quiet_run.returncode != 0:
            return fail(f"intensity 0 render exited {quiet_run.returncode}")
        quiet_dir = os.path.join(quiet_out, "ukmin", "roll_00")
        quiet_wavs = os.listdir(quiet_dir) if os.path.isdir(quiet_dir) else []
        quiet_mixes = [n for n in quiet_wavs if n.endswith(".wav") and n.count("_") == 2]
        if not quiet_mixes:
            problems.append("intensity 0 produced no mix")
        else:
            r = measure(os.path.join(quiet_dir, quiet_mixes[0]))
            print(f"  intensity 0 tp={r['tp_dbtp']:.2f} lead={r['lead_silence_ms']}")
            if r["tp_dbtp"] is None or r["tp_dbtp"] > -6.0:
                problems.append(f"intensity 0 true peak {r['tp_dbtp']} above -6")
            if r["lead_silence_ms"] is None or r["lead_silence_ms"] >= 12.0:
                problems.append(f"intensity 0 lead silence {r['lead_silence_ms']}")

        if problems:
            for p in problems:
                print("FAIL", p)
            return 1
    print("renderer smoke ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
