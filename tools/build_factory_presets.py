#!/usr/bin/env python3
"""Grow resources/presets.json so every manifest file is referenced.

Hero presets stay first (dialect defaults). Each melodic file then gets its
own SAMPLE/LAYER preset. One clearly named SYNTH preset is appended per lane.
Drum kits keep the circuit 909 as "909 SYNTH" and give every voice a pool
drawn from its manifest folder.
"""

import copy
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRESETS = os.path.join(ROOT, "resources", "presets.json")
MANIFEST = os.path.join(ROOT, "resources", "SAMPLE_MANIFEST.json")

HERO_STYLES = {
    ("bass", "pluck"): ["ukmin"],
    ("bass", "round"): ["long", "gaskin"],
    ("bass", "click"): ["pop"],
    ("stab", "dub"): ["ukmin"],
    ("stab", "swell"): ["long", "gaskin"],
    ("stab", "click"): ["pop"],
    ("hits", "warehouse"): ["ukmin", "gaskin"],
    ("hits", "rave"): ["pop"],
    ("hits", "dusty"): ["long"],
    ("pluck", "hook"): ["ukmin", "gaskin"],
    ("pluck", "soft"): ["pop"],
    ("pluck", "bright"): ["long"],
    ("pad", "air"): ["ukmin", "pop"],
    ("pad", "wide"): ["long"],
    ("pad", "dark"): ["gaskin"],
    ("vox", "close"): ["ukmin"],
    ("vox", "booth"): ["long"],
    ("vox", "chop"): ["pop", "gaskin"],
    ("drums", "tight"): ["ukmin", "long", "pop"],
    ("drums", "room"): ["gaskin"],
}

LANE_ID = {
    "bass": "bb",
    "stab": "sb",
    "hits": "hb",
    "pluck": "pb",
    "pad": "ab",
    "vox": "vb",
}

MELODIC = ("bass", "stab", "hits", "pluck", "pad", "vox")
LANE_FOLDER = {p: p.upper() for p in MELODIC}

VOICE_KEYS = {
    "KICK": ("kick",),
    "SNARE": ("snare",),
    "CLAP": ("clap",),
    "RIM": ("rim",),
    "CH": ("hatC",),
    "OH": ("hatO",),
    "RIDE": ("ride",),
    "PERC": ("perc", "tomLow", "tomMid"),
}


def styles_from_tags(tags):
    found = []
    for tag in tags or []:
        parts = str(tag).split(".")
        if len(parts) >= 2 and parts[1] in ("ukmin", "long", "pop", "gaskin"):
            if parts[1] not in found:
                found.append(parts[1])
    return found


def clip_name(text):
    text = " ".join(text.split())
    if len(text) <= 31:
        return text
    return text[:31].rstrip()


def sample_block(meta, lane):
    prep = meta.get("prep") or {}
    length = float(prep.get("length_ms") or 0)
    frames = int(prep.get("frames") or 0)
    sr = int(prep.get("sr") or 44100)
    lead = float(prep.get("lead_ms_at_-60dBFS") or 0)
    start = round(max(0.0, lead) / 1000.0, 4)
    root = meta.get("root") or "C3"
    mode = "SAMPLE"
    mix = 1.0
    loop_on = False
    slice_on = False
    if lane == "bass":
        mode = "LAYER"
        mix = 0.45 if length < 400 else (0.6 if length >= 800 else 0.55)
        loop_on = length >= 800
    elif lane == "stab":
        mode = "LAYER" if length >= 700 else "SAMPLE"
        mix = 0.6 if mode == "LAYER" else 1.0
        loop_on = length >= 1200
    elif lane == "hits":
        mode = "SAMPLE"
        mix = 1.0
    elif lane == "pluck":
        mode = "LAYER" if length >= 800 else "SAMPLE"
        mix = 0.55 if mode == "LAYER" else 1.0
    elif lane == "pad":
        mode = "LAYER"
        mix = 0.6
        loop_on = True
    elif lane == "vox":
        mode = "SAMPLE"
        mix = 1.0
        slice_on = length >= 600

    loop = {"on": False, "start": 0, "end": 0, "xfade": 0}
    if loop_on and frames > 128 and sr > 1000:
        fade_fr = int(sr * float(prep.get("fadeOut_ms") or 12) / 1000.0)
        end = max(64, frames - max(fade_fr, 1))
        start_fr = max(48, int(frames * 0.18))
        if start_fr >= end - 64:
            start_fr = max(48, end // 3)
        xfade = max(48, int(sr * 0.04))
        xfade = min(xfade, max(48, (end - start_fr) // 4))
        loop = {"on": True, "start": int(start_fr), "end": int(end), "xfade": int(xfade)}

    return {
        "mode": mode,
        "file": meta["file"],
        "root": root,
        "start": start,
        "reverse": False,
        "slice": slice_on,
        "loop": loop,
        "layerMix": mix,
        "pool": [],
    }


def synth_block():
    return {
        "mode": "SYNTH",
        "file": "",
        "root": "C3",
        "start": 0.0,
        "reverse": False,
        "slice": False,
        "loop": {"on": False, "start": 0, "end": 0, "xfade": 0},
        "layerMix": 0.0,
        "pool": [],
    }


def order_pool(files, primary):
    ordered = []
    if primary:
        ordered.append(primary)
    for path in files:
        if path not in ordered:
            ordered.append(path)
    return ordered


def main():
    with open(PRESETS, encoding="utf-8") as handle:
        doc = json.load(handle)
    with open(MANIFEST, encoding="utf-8") as handle:
        manifest = json.load(handle)

    by_lane = {lane: [] for lane in MELODIC}
    drums = {folder: [] for folder in VOICE_KEYS}
    for entry in manifest["files"]:
        lane = str(entry.get("lane") or "").lower()
        if lane in by_lane:
            by_lane[lane].append(entry)
        elif lane == "drums":
            folder = (entry.get("voice") or "").upper()
            if folder in drums:
                drums[folder].append(entry["file"])

    parts = doc["parts"]
    for lane in MELODIC:
        kept = []
        for preset in parts[lane]["presets"]:
            if preset.get("library"):
                continue
            if preset.get("id") == "synth":
                continue
            styles = HERO_STYLES.get((lane, preset.get("id")))
            if styles:
                preset["styles"] = styles
            kept.append(preset)
        if not kept:
            raise SystemExit(f"{lane} has no hero preset to copy")
        template = copy.deepcopy(kept[0])
        used_names = {p.get("name") for p in kept}
        for index, meta in enumerate(by_lane[lane]):
            preset = copy.deepcopy(template)
            preset["id"] = f"{LANE_ID[lane]}{index:02d}"
            preset["library"] = True
            base = os.path.splitext(os.path.basename(meta["file"]))[0].replace("_", " ")
            name = clip_name(base)
            if name in used_names:
                name = clip_name(f"{index:02d} {base}")
            used_names.add(name)
            preset["name"] = name
            preset["sample"] = sample_block(meta, lane)
            styles = styles_from_tags(meta.get("presets"))
            if styles:
                preset["styles"] = styles
            else:
                preset.pop("styles", None)
            kept.append(preset)
        synth = copy.deepcopy(template)
        synth["id"] = "synth"
        synth["name"] = "SYNTH"
        synth.pop("library", None)
        synth.pop("styles", None)
        synth["sample"] = synth_block()
        kept.append(synth)
        parts[lane]["presets"] = kept

    for preset in parts["drums"]["presets"]:
        ident = preset.get("id")
        if ident in ("dry", "synth") or preset.get("name") in ("909 Dry", "909 SYNTH"):
            preset["id"] = "synth"
            preset["name"] = "909 SYNTH"
            preset.pop("styles", None)
            sample = preset.setdefault("sample", {})
            sample["mode"] = "SYNTH"
            sample["file"] = ""
            sample["pool"] = []
            sample.pop("voicePools", None)
            voices = sample.setdefault("voices", {})
            for key in ("kick", "snare", "clap", "rim", "tomLow", "tomMid", "hatC", "hatO", "ride", "perc"):
                voices[key] = ""
            continue
        styles = HERO_STYLES.get(("drums", ident))
        if styles:
            preset["styles"] = styles
        sample = preset.setdefault("sample", {})
        sample["mode"] = "SAMPLE"
        sample["layerMix"] = 1.0
        voices = sample.setdefault("voices", {})
        pools = {}
        for folder, keys in VOICE_KEYS.items():
            files = drums.get(folder) or []
            for key in keys:
                primary = voices.get(key) or ""
                if not isinstance(primary, str):
                    primary = ""
                pool = order_pool(files, primary)
                if not pool:
                    continue
                voices[key] = pool[0]
                pools[key] = pool
        sample["voicePools"] = pools

    doc["notes"] = (
        "Schema v3. Factory melodic presets are SAMPLE or LAYER and point at "
        "files under the samples root. One SYNTH preset per lane keeps the "
        "circuit voice. Drum kits sample every voice from DRUMS/{KICK,SNARE,"
        "CLAP,RIM,CH,OH,RIDE,PERC}; 909 SYNTH is the circuit kit and the "
        "missing-file fallback. Do not commit audio."
    )

    with open(PRESETS, "w", encoding="utf-8") as handle:
        json.dump(doc, handle, indent=2, ensure_ascii=False)
        handle.write("\n")

    counts = {lane: len(parts[lane]["presets"]) for lane in list(MELODIC) + ["drums"]}
    print("preset counts", counts)
    print("max", max(counts.values()))


if __name__ == "__main__":
    sys.exit(main())
