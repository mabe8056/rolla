#!/usr/bin/env python3
"""Turn factory presets into sound recipes.

Hero presets stay first so dialect ids still resolve. Each one is SAMPLE mode
with a pool filter over its whole lane, so ROLL draws from whatever the
scanner finds. A single file hint remains for the moment the folder is empty.
VOX gains phrase and chop recipes. PAD gains an atmosphere recipe.
Drum kits do not embed voice pools; the DRUMS scan is the pool.
"""

import copy
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRESETS = os.path.join(ROOT, "resources", "presets.json")

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
EXTRA_IDS = {"phrases", "chops", "atmospheres"}


def is_generated(preset, lane):
    if preset.get("library"):
        return True
    ident = str(preset.get("id") or "")
    if ident in EXTRA_IDS or ident == "synth":
        return True
    prefix = LANE_ID[lane]
    return len(ident) == len(prefix) + 2 and ident.startswith(prefix) and ident[-2:].isdigit()


def recipe(preset, lane, category=None):
    sample = preset.setdefault("sample", {})
    sample["mode"] = "SAMPLE"
    sample["pool"] = []
    filt = {"lane": lane.upper()}
    if category:
        filt["category"] = category
    sample["poolFilter"] = filt
    preset.pop("library", None)
    return preset


def clone_recipe(source, ident, name, lane, category):
    preset = copy.deepcopy(source)
    preset["id"] = ident
    preset["name"] = name
    preset.pop("styles", None)
    preset.pop("library", None)
    return recipe(preset, lane, category)


def main():
    with open(PRESETS, encoding="utf-8") as handle:
        doc = json.load(handle)

    parts = doc["parts"]
    for lane in MELODIC:
        kept = []
        for preset in parts[lane]["presets"]:
            if is_generated(preset, lane):
                continue
            styles = HERO_STYLES.get((lane, preset.get("id")))
            if styles:
                preset["styles"] = styles
            kept.append(recipe(preset, lane))
        if len(kept) < 1:
            raise SystemExit(f"{lane} has no hero preset")
        if lane == "vox":
            kept.append(clone_recipe(kept[0], "phrases", "Phrases", lane, "phrase"))
            kept.append(clone_recipe(kept[0], "chops", "Chops", lane, "chop"))
        elif lane == "pad":
            kept.append(clone_recipe(kept[0], "atmospheres", "Atmospheres", lane, "atmosphere"))
        parts[lane]["presets"] = kept

    kits = []
    for preset in parts["drums"]["presets"]:
        ident = preset.get("id")
        if ident in ("dry", "synth") or preset.get("name") in ("909 Dry", "909 SYNTH"):
            continue
        if preset.get("library"):
            continue
        styles = HERO_STYLES.get(("drums", ident))
        if styles:
            preset["styles"] = styles
        sample = preset.setdefault("sample", {})
        sample["mode"] = "SAMPLE"
        sample["layerMix"] = 1.0
        sample.pop("voicePools", None)
        sample["pool"] = []
        sample["poolFilter"] = {"lane": "DRUMS"}
        kits.append(preset)
    if len(kits) < 1:
        raise SystemExit("no sample drum kit")
    parts["drums"]["presets"] = kits

    doc["notes"] = (
        "Schema v3. Factory presets are sample recipes. Each melodic recipe "
        "filters the scanned lane folder (root, pack and category when set) "
        "so ROLL draws from every file in that pool. VOX has phrase and chop "
        "recipes. PAD has an atmosphere recipe. Drum kits read "
        "DRUMS/<voice>/ from the index and do not embed file lists. "
        "A missing hit stays on another of his samples. There is no synth "
        "drum kit. Do not commit audio."
    )

    with open(PRESETS, "w", encoding="utf-8") as handle:
        json.dump(doc, handle, indent=2, ensure_ascii=False)
        handle.write("\n")

    counts = {lane: len(parts[lane]["presets"]) for lane in list(MELODIC) + ["drums"]}
    print("preset counts", counts)


if __name__ == "__main__":
    sys.exit(main())
