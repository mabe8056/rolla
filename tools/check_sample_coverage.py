#!/usr/bin/env python3
"""Fail if a factory preset names a file outside SAMPLE_MANIFEST.json, or if
any manifest file is unused. Every default preset must reference a sample.
A synth drum kit is a failure.
"""

import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRESETS = os.path.join(ROOT, "resources", "presets.json")
MANIFEST = os.path.join(ROOT, "resources", "SAMPLE_MANIFEST.json")

MELODIC = ("bass", "stab", "hits", "pluck", "pad", "vox")
DRUM_VOICES = ("kick", "snare", "clap", "rim", "hatC", "hatO", "ride", "perc")
AUDIO = (".wav", ".aif", ".aiff", ".flac")


def walk_paths(node, found):
    if isinstance(node, str):
        text = node.replace("\\", "/")
        if "/" in text and text.lower().endswith(AUDIO):
            found.add(text)
    elif isinstance(node, dict):
        for value in node.values():
            walk_paths(value, found)
    elif isinstance(node, list):
        for value in node:
            walk_paths(value, found)


def main():
    with open(PRESETS, encoding="utf-8") as handle:
        presets = json.load(handle)
    with open(MANIFEST, encoding="utf-8") as handle:
        manifest = json.load(handle)

    manifest_files = [entry["file"].replace("\\", "/") for entry in manifest["files"]]
    manifest_set = set(manifest_files)
    if len(manifest_files) != len(manifest_set):
        print("FAIL duplicate paths in SAMPLE_MANIFEST.json")
        return 1
    declared = manifest.get("count")
    if declared is not None and declared != len(manifest_set):
        print(f"FAIL manifest count {declared} != {len(manifest_set)} files")
        return 1

    referenced = set()
    walk_paths(presets.get("parts", {}), referenced)

    unknown = sorted(referenced - manifest_set)
    unused = sorted(manifest_set - referenced)
    print(f"manifest {len(manifest_set)}")
    print(f"referenced {len(referenced)}")
    print(f"used {len(manifest_set & referenced)}")
    print(f"unknown {len(unknown)}")
    print(f"unused {len(unused)}")

    failed = False
    if unknown:
        failed = True
        print("FAIL preset files missing from SAMPLE_MANIFEST.json:")
        for path in unknown:
            print(f"  {path}")
    if unused:
        failed = True
        print("FAIL manifest files not used by any preset:")
        for path in unused:
            print(f"  {path}")

    parts = presets["parts"]
    for lane in MELODIC:
        synths = 0
        for preset in parts[lane]["presets"]:
            sample = preset.get("sample") or {}
            mode = str(sample.get("mode", "SYNTH")).upper()
            if mode != "SAMPLE" or not sample.get("file"):
                failed = True
                synths += 1 if mode == "SYNTH" else 0
                print(f"FAIL {lane} preset {preset.get('id')} lacks a sample reference ({mode})")
        print(f"{lane} presets {len(parts[lane]['presets'])} synth {synths}")

    sampled = 0
    default_kit = parts["drums"]["presets"][0]
    default_mode = str((default_kit.get("sample") or {}).get("mode", "SYNTH")).upper()
    if default_mode != "SAMPLE":
        failed = True
        print(f"FAIL default drum kit {default_kit.get('id')} is {default_mode}, not SAMPLE")
    for preset in parts["drums"]["presets"]:
        sample = preset.get("sample") or {}
        mode = str(sample.get("mode", "SYNTH")).upper()
        if mode != "SAMPLE":
            failed = True
            print(f"FAIL kit {preset.get('id')} is {mode}; factory drums are samples")
            continue
        sampled += 1
        pools = sample.get("voicePools") or {}
        for voice in DRUM_VOICES:
            pool = pools.get(voice) or []
            primary = (sample.get("voices") or {}).get(voice) or ""
            if not pool or not primary:
                failed = True
                print(f"FAIL kit {preset.get('id')} voice {voice} has no sample pool")
        for voice in ("tomLow", "tomMid"):
            pool = pools.get(voice) or []
            if not pool:
                failed = True
                print(f"FAIL kit {preset.get('id')} {voice} is not covered by the perc pool")
    print(f"drums sample kits {sampled}")

    if failed:
        return 1
    print("sample coverage ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
