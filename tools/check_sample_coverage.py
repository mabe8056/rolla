#!/usr/bin/env python3
"""Factory recipes must be sample filters, not one preset per file.

Every melodic lane and every drum voice is reachable from a broad recipe.
An explicit path, when a recipe still names one, has to be in the manifest.
Files that are only on disk are reached by the folder scan, so the manifest
does not have to be copied into presets.json.
"""

import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRESETS = os.path.join(ROOT, "resources", "presets.json")
MANIFEST = os.path.join(ROOT, "resources", "SAMPLE_MANIFEST.json")

MELODIC = ("bass", "stab", "hits", "pluck", "pad", "vox")
LANE_FOLDER = {lane: lane.upper() for lane in MELODIC}
DRUM_VOICES = {
    "KICK": "kick",
    "SNARE": "snare",
    "CLAP": "clap",
    "RIM": "rim",
    "CH": "hatC",
    "OH": "hatO",
    "RIDE": "ride",
    "PERC": "perc",
}
AUDIO = (".wav", ".aif", ".aiff", ".flac")
MAX_PRESETS = 16
MAX_LIST = 8
MAX_EXPLICIT = 96


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


def sample_of(preset):
    return preset.get("sample") or {}


def filt_of(preset):
    return sample_of(preset).get("poolFilter") or {}


def is_broad(preset, lane_folder):
    filt = filt_of(preset)
    if str(filt.get("lane") or "").upper() != lane_folder:
        return False
    if filt.get("category") or filt.get("pack") or filt.get("voice"):
        return False
    root = filt.get("root")
    if root not in (None, ""):
        return False
    return True


def has_category(presets, category):
    want = category.lower()
    for preset in presets:
        if str(filt_of(preset).get("category") or "").lower() == want:
            return True
    return False


def has_phrase_pool(presets, loops_only):
    for preset in presets:
        filt = filt_of(preset)
        if str(filt.get("category") or "").lower() != "phrase":
            continue
        marked = "loop" in filt
        only = int(filt.get("loop") or 0) == 1
        if loops_only and only:
            return True
        if not loops_only and not marked:
            return True
    return False


def alt_names(entry):
    return [str(item).upper() for item in (entry.get("alt_lanes") or [])]


def list_too_long(node, where, failed):
    if isinstance(node, dict):
        for key, value in node.items():
            if key in ("pool", "voicePools") and isinstance(value, list) and len(value) > MAX_LIST:
                failed.append(f"FAIL {where} {key} lists {len(value)} files")
            elif key == "voicePools" and isinstance(value, dict):
                for voice, pool in value.items():
                    if isinstance(pool, list) and len(pool) > MAX_LIST:
                        failed.append(f"FAIL {where} {voice} lists {len(pool)} files")
            else:
                list_too_long(value, where, failed)
    elif isinstance(node, list):
        for value in node:
            list_too_long(value, where, failed)


def main():
    with open(PRESETS, encoding="utf-8") as handle:
        presets = json.load(handle)
    with open(MANIFEST, encoding="utf-8") as handle:
        manifest = json.load(handle)

    files = manifest["files"]
    manifest_files = [entry["file"].replace("\\", "/") for entry in files]
    manifest_set = set(manifest_files)
    if len(manifest_files) != len(manifest_set):
        print("FAIL duplicate paths in SAMPLE_MANIFEST.json")
        return 1
    declared = manifest.get("count")
    if declared != 2697 or len(manifest_set) != 2697:
        print(f"FAIL manifest v2 count {declared} / {len(manifest_set)}, want 2697")
        return 1
    if manifest.get("version") != 2:
        print(f"FAIL manifest version {manifest.get('version')}, want 2")
        return 1
    phrase_loops = sum(1 for entry in files if entry.get("category") == "vox_phrase_loop")
    stab_as_pluck = sum(
        1 for entry in files
        if str(entry.get("lane") or "").upper() == "STAB" and "PLUCK" in alt_names(entry)
    )
    pluck_as_stab = sum(
        1 for entry in files
        if str(entry.get("lane") or "").upper() == "PLUCK" and "STAB" in alt_names(entry)
    )
    rim_as_perc = sum(
        1 for entry in files
        if str(entry.get("lane") or "").upper() == "DRUMS"
        and str(entry.get("voice") or "").upper() == "RIM"
        and any("PERC" in name for name in alt_names(entry))
    )
    print(f"vox_phrase_loop {phrase_loops}")
    print(f"alt stab/pluck {stab_as_pluck} pluck/stab {pluck_as_stab} rim/perc {rim_as_perc}")
    if phrase_loops < 140:
        print(f"FAIL vox_phrase_loop {phrase_loops}, want at least 140")
        return 1
    if stab_as_pluck < 50 or pluck_as_stab < 50 or rim_as_perc < 20:
        print("FAIL alt_lanes did not mark both STAB/PLUCK and RIM/PERC")
        return 1

    referenced = set()
    walk_paths(presets.get("parts", {}), referenced)
    unknown = sorted(referenced - manifest_set)
    print(f"manifest {len(manifest_set)}")
    print(f"explicit {len(referenced)}")
    print(f"unknown {len(unknown)}")

    failed = []
    if unknown:
        failed.append("FAIL preset files missing from SAMPLE_MANIFEST.json")
        failed.extend(f"  {path}" for path in unknown)
    if len(referenced) > MAX_EXPLICIT:
        failed.append(f"FAIL {len(referenced)} explicit files; recipes must scan pools")

    parts = presets["parts"]
    lanes_in_manifest = {}
    voices_in_manifest = set()
    for entry in manifest["files"]:
        lane = str(entry.get("lane") or "").upper()
        lanes_in_manifest.setdefault(lane, 0)
        lanes_in_manifest[lane] += 1
        if lane == "DRUMS":
            voices_in_manifest.add(str(entry.get("voice") or "").upper())

    for lane in MELODIC:
        rows = parts[lane]["presets"]
        print(f"{lane} presets {len(rows)}")
        if len(rows) > MAX_PRESETS:
            failed.append(f"FAIL {lane} has {len(rows)} presets (one-per-file regression)")
        if not any(is_broad(preset, LANE_FOLDER[lane]) for preset in rows):
            failed.append(f"FAIL {lane} has no recipe over the whole lane")
        list_too_long(rows, lane, failed)
        for preset in rows:
            sample = sample_of(preset)
            mode = str(sample.get("mode", "SYNTH")).upper()
            filt = filt_of(preset)
            if mode != "SAMPLE" or not str(filt.get("lane") or ""):
                failed.append(f"FAIL {lane} preset {preset.get('id')} is not a sample recipe ({mode})")

    if not has_phrase_pool(parts["vox"]["presets"], False):
        failed.append("FAIL vox has no phrase recipe that includes one-shots")
    if not has_phrase_pool(parts["vox"]["presets"], True):
        failed.append("FAIL vox has no tempo-locked phrase-loop recipe")
    if not has_category(parts["vox"]["presets"], "chop"):
        failed.append("FAIL vox has no chop recipe")
    if not has_category(parts["pad"]["presets"], "atmosphere"):
        failed.append("FAIL pad has no atmosphere recipe")

    kits = parts["drums"]["presets"]
    print(f"drums presets {len(kits)}")
    if len(kits) > MAX_PRESETS:
        failed.append(f"FAIL drums has {len(kits)} presets")
    if not kits or str(sample_of(kits[0]).get("mode", "SYNTH")).upper() != "SAMPLE":
        failed.append("FAIL default drum kit is not SAMPLE")
    if not any(is_broad(preset, "DRUMS") for preset in kits):
        failed.append("FAIL drums has no recipe over the whole pack")
    list_too_long(kits, "drums", failed)
    for preset in kits:
        sample = sample_of(preset)
        mode = str(sample.get("mode", "SYNTH")).upper()
        if mode != "SAMPLE":
            failed.append(f"FAIL kit {preset.get('id')} is {mode}; factory drums are samples")
        if sample.get("voicePools"):
            failed.append(f"FAIL kit {preset.get('id')} embeds voicePools")

    for lane, count in lanes_in_manifest.items():
        if lane in LANE_FOLDER.values() and not any(
            is_broad(preset, lane) for preset in parts[lane.lower()]["presets"]
        ):
            failed.append(f"FAIL manifest lane {lane} ({count} files) is not covered by a recipe")
        if lane == "DRUMS":
            for folder in voices_in_manifest:
                if folder and folder not in DRUM_VOICES and folder not in ("", "NONE", "NULL"):
                    # Unknown voice folders are still inside the broad DRUMS recipe.
                    pass
            if not any(is_broad(preset, "DRUMS") for preset in kits):
                failed.append("FAIL drum manifest is not covered by a recipe")

    for line in failed:
        print(line)
    if failed:
        return 1
    print("phrase chop loop atmosphere recipes ok")
    print("sample coverage ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
