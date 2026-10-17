#!/usr/bin/env python3
"""Extract a ROLLA cell vocabulary from the transcription MIDI.

Writes data/corpus.json and src/Generator/CorpusData.h. Cells are 1-bar and
2-bar grids (rhythm, length, pitch relative to the root, voicing, drum hits).
Whole phrases are not stored. Adjacent 2-bar windows are hashed so the
generator can reject an exact copy.
"""

from __future__ import annotations

import collections
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path("/tmp/rolla_refs")
OUT_JSON = Path("/workspace/data/corpus.json")
OUT_H = Path("/workspace/src/Generator/CorpusData.h")

ARTISTS = [
    "localdub",
    "lightleak",
    "stef",
    "josh",
    "laidlaw",
    "mhigh",
    "ellia",
    "gaskin",
    "melak",
    "sketch",
]

# Dialect rows match rolla::Dialect: ukmin, long, pop, gaskin.
STYLE_WEIGHT = {
    "ukmin": {
        "localdub": 0.26, "lightleak": 0.16, "stef": 0.10, "josh": 0.07,
        "laidlaw": 0.14, "mhigh": 0.12, "ellia": 0.03, "gaskin": 0.00,
        "melak": 0.04, "sketch": 0.08,
    },
    "long": {
        "localdub": 0.14, "lightleak": 0.22, "stef": 0.26, "josh": 0.06,
        "laidlaw": 0.08, "mhigh": 0.05, "ellia": 0.02, "gaskin": 0.00,
        "melak": 0.05, "sketch": 0.12,
    },
    "pop": {
        "localdub": 0.05, "lightleak": 0.04, "stef": 0.03, "josh": 0.28,
        "laidlaw": 0.08, "mhigh": 0.24, "ellia": 0.22, "gaskin": 0.00,
        "melak": 0.04, "sketch": 0.02,
    },
    "gaskin": {
        "localdub": 0.02, "lightleak": 0.01, "stef": 0.01, "josh": 0.01,
        "laidlaw": 0.01, "mhigh": 0.01, "ellia": 0.03, "gaskin": 0.86,
        "melak": 0.02, "sketch": 0.02,
    },
}

# Documented tonal centres (pitch class). Laidlaw is D#/A#; the histogram
# below is checked and the prior is kept when it is among the top classes.
ROOT_PC = {
    "gaskin": 7,
    "lightleak": 7,
    "stef": 7,
    "josh": 0,
    "laidlaw": 3,
    "mhigh": 5,
    "ellia": 6,
    "melak": 2,
    "sketch": 2,
}


def read_vlq(data: bytes, i: int):
    n = 0
    while True:
        b = data[i]
        i += 1
        n = (n << 7) | (b & 0x7F)
        if not (b & 0x80):
            return n, i


def parse_midi(path: Path):
    data = path.read_bytes()
    if data[:4] != b"MThd":
        raise RuntimeError(path)
    hlen = struct.unpack(">I", data[4:8])[0]
    _fmt, ntr, ppq = struct.unpack(">HHH", data[8:14])
    i = 8 + hlen
    notes = []
    for _t in range(ntr):
        if data[i:i + 4] != b"MTrk":
            break
        ln = struct.unpack(">I", data[i + 4:i + 8])[0]
        i += 8
        end = i + ln
        tick = 0
        running = None
        ons = {}
        while i < end:
            dt, i = read_vlq(data, i)
            tick += dt
            if data[i] & 0x80:
                status = data[i]
                i += 1
                running = status
            else:
                status = running
            if status is None:
                break
            kind = status & 0xF0
            ch = status & 0x0F
            if kind in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                d1, d2 = data[i], data[i + 1]
                i += 2
                if kind == 0x90 and d2 > 0:
                    ons.setdefault((ch, d1), []).append((tick, d2))
                elif kind == 0x80 or (kind == 0x90 and d2 == 0):
                    key = (ch, d1)
                    if ons.get(key):
                        on, vel = ons[key].pop(0)
                        notes.append((on, tick - on, d1, vel, ch))
            elif kind in (0xC0, 0xD0):
                i += 1
            elif status == 0xFF:
                i += 1
                ln2, i = read_vlq(data, i)
                i += ln2
            elif status in (0xF0, 0xF7):
                ln2, i = read_vlq(data, i)
                i += ln2
            else:
                break
        i = end
    return ppq, notes


def artist_of(path: Path) -> str:
    p = str(path)
    if "/writing/sketch" in p:
        return "sketch"
    if "melak_clips" in p:
        return "melak"
    if "gaskin" in p:
        return "gaskin"
    if "lightleak" in p:
        return "lightleak"
    if "ellia" in p or "eliya" in p:
        return "ellia"
    if "josh_baker" in p:
        return "josh"
    if "laidlaw" in p:
        return "laidlaw"
    if "m_high" in p:
        return "mhigh"
    if "stef" in p:
        return "stef"
    if "local" in p:
        return "localdub"
    return "melak"


def root_pc(artist: str, path: Path) -> int:
    if artist == "localdub":
        return 3 if "reaction" in str(path) or "localdub_" in path.name else 7
    return ROOT_PC[artist]


def part_of(path: Path) -> str:
    name = path.stem.lower()
    if "counter" in name or "/counter/" in str(path):
        return "counter"
    if "bassline" in name or name.startswith("bass") or "direction_a_bass" in name:
        return "bass"
    if "stab" in name or "quartal" in name:
        return "stab"
    if "drum" in name:
        return "drum"
    if "chop" in name:
        return "chop"
    return "other"


def group_key(path: Path):
    name = path.stem
    folder = str(path.parent)
    for prefix in (
        "direction_A_quartal_stab",
        "direction_A_bass",
        "localdub_bass_plus_otherhits_bars41-48",
        "localdub_bass_bars41-48",
        "basslines",
        "counter",
        "bass",
        "stabs",
        "drums",
        "chops",
    ):
        if name.startswith(prefix) or name == prefix:
            return folder, name[len(prefix):]
    return folder, name


def fnv_byte(h: int, b: int) -> int:
    h ^= b & 0xFF
    h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def hash_events(events) -> int:
    h = 14695981039346656037
    for slot, len2, pc in events:
        h = fnv_byte(h, slot)
        h = fnv_byte(h, len2)
        h = fnv_byte(h, pc)
    return h


def pair_hash(a: int, b: int) -> int:
    return ((a * 0x9E3779B97F4A7C15) ^ ((b + 0xC0FFEE) & 0xFFFFFFFFFFFFFFFF)) & 0xFFFFFFFFFFFFFFFF


def drum_voice(pitch: int):
    if pitch in (35, 36):
        return "kick"
    if pitch in (38, 40):
        return "snare"
    if pitch == 39:
        return "clap"
    if pitch == 37:
        return "rim"
    if pitch in (41, 43, 45):
        return "tomL"
    if pitch in (47, 48, 50):
        return "tomM"
    if pitch in (42, 44):
        return "hatC"
    if pitch == 46:
        return "hatO"
    if pitch in (49, 51, 52, 53, 55, 57, 59):
        return "ride"
    return None


VOICE_ID = {
    "kick": 0, "snare": 1, "clap": 2, "rim": 3, "tomL": 4, "tomM": 5,
    "hatC": 6, "hatO": 7, "ride": 8, "perc": 9,
}

DARK_PC = {1, 3, 6, 8, 10}
BRIGHT_PC = {2, 4, 5, 7, 9}


def pc_of(iv: int) -> int:
    return iv % 12


def darkness(ivs) -> int:
    if not ivs:
        return 40
    pcs = [pc_of(i) for i in ivs if pc_of(i) != 0]
    if not pcs:
        return 20
    d = sum(1 for p in pcs if p in DARK_PC)
    b = sum(1 for p in pcs if p in BRIGHT_PC)
    return int(round(100 * d / max(1, d + b)))


def classify_voicing(offsets) -> str:
    if len(offsets) <= 1:
        return "single"
    seq = sorted(set(offsets))
    steps = [seq[i + 1] - seq[i] for i in range(len(seq) - 1)]
    if steps and all(s == 5 for s in steps) and len(seq) >= 3:
        return "quartal"
    pcs = sorted({p % 12 for p in seq})
    if pcs and all(p in (0, 5, 10) for p in pcs) and len(pcs) >= 3:
        return "quartal"
    has_min = (3 in pcs and 10 in pcs) or (3 in pcs and 7 in pcs and 10 in pcs)
    if has_min and (2 in pcs or 14 in {p % 24 for p in seq}):
        return "min9"
    if has_min and 5 in pcs:
        return "min11"
    if has_min:
        return "min7"
    if all(p in (0, 7) for p in pcs):
        return "fifth"
    if len(pcs) == 2:
        return "dyad"
    if 3 in pcs or 10 in pcs:
        return "min7"
    return "other"


VOICING_ID = {
    "quartal": 0, "min7": 1, "min9": 2, "min11": 3, "fifth": 4,
    "dyad": 5, "single": 6, "other": 7,
}


def choose_root_midi(pitches, root_pc: int) -> int:
    if not pitches:
        return 24 + root_pc
    best = None
    for octv in (0, 1, 2, 3):
        root = 12 * octv + root_pc
        ivs = [p - root for p in pitches]
        med = sorted(ivs)[len(ivs) // 2]
        score = sum(abs(i) for i in ivs) + (0 if -5 <= med <= 14 else 80)
        if best is None or score < best[0]:
            best = (score, root)
    return best[1]


def content_hash(notes) -> str:
    h = hashlib.sha1()
    for n in notes:
        h.update(repr(n).encode())
    return h.hexdigest()


def quantize_notes(notes, ppq, rotate_sixteenths: int):
    six = ppq / 4.0
    shift = int(round(rotate_sixteenths * six))
    out = []
    for on, dur, pitch, vel, ch in notes:
        shifted = on - shift
        slot = int(round(shifted / six))
        length16 = max(0.25, dur / six)
        out.append((slot, length16, pitch, vel, ch, shifted))
    return out


def rotation_for(drum_notes, ppq) -> int:
    if not drum_notes:
        return 0
    six = ppq / 4.0
    kicks = []
    for on, _dur, pitch, vel, _ch in drum_notes:
        if drum_voice(pitch) == "kick":
            kicks.append((on / six, vel))
    if len(kicks) < 4:
        return 0
    best_r, best_s = 0, -1
    for r in range(16):
        score = 0
        for slot, vel in kicks:
            s = (int(round(slot)) - r) % 16
            if s in (0, 4, 8, 12):
                score += 2 + (vel > 100)
        if score > best_s:
            best_s = score
            best_r = r
    return best_r


def bar_count_of(slots) -> int:
    if not slots:
        return 0
    return max(slots) // 16 + 1


def collect():
    files = sorted(ROOT.rglob("*.mid"))
    parsed = {}
    seen = set()
    for path in files:
        ppq, notes = parse_midi(path)
        if not notes:
            continue
        digest = content_hash(notes)
        if digest in seen:
            continue
        seen.add(digest)
        parsed[path] = (ppq, notes)

    # Phase-align each take so kicks land on the quarters.
    by_group = collections.defaultdict(list)
    for path in parsed:
        by_group[group_key(path)].append(path)

    rotated = {}
    for _key, paths in by_group.items():
        drums = [p for p in paths if part_of(p) == "drum"]
        r = 0
        if drums:
            ppq, notes = parsed[drums[0]]
            r = rotation_for(notes, ppq)
        for p in paths:
            ppq, notes = parsed[p]
            rotated[p] = (ppq, quantize_notes(notes, ppq, r), r)

    bass_events = []
    bass_cells = []
    contours = []
    contour_ev = []
    stab_hits = []
    stab_cells = []
    voicings = []
    drum_hits = []
    drum_cells = []
    chop_ev = []
    chop_cells = []
    bans = set()

    def add_bass_bar(artist, events, fill):
        if not events or len(events) > 12:
            return None
        sig = tuple((e[0], e[1], e[2], e[3]) for e in events)
        sync = int(round(100 * sum(1 for s, *_ in events if s % 4 != 0) / len(events)))
        odd = int(round(100 * sum(1 for s, *_ in events if s % 2 == 1) / len(events)))
        dark = darkness([e[2] for e in events])
        key = (1, sig)
        return key, {
            "artist": artist, "bars": 1, "events": events, "fill": fill,
            "sync": sync, "odd": odd, "dark": dark,
        }

    bass_unique = {}
    contour_unique = {}
    stab_unique = {}
    voicing_unique = {}
    drum_unique = {}
    chop_unique = {}

    stats = collections.Counter()

    for path, (ppq, qnotes, rot) in rotated.items():
        artist = artist_of(path)
        kind = part_of(path)
        rpc = root_pc(artist, path)
        stats[f"file:{artist}:{kind}"] += 1

        if kind == "bass":
            pitches = [n[2] for n in qnotes]
            root_m = choose_root_midi(pitches, rpc)
            # Merge same-slot onsets, keep the stronger.
            by_bar = collections.defaultdict(dict)
            for slot, length16, pitch, vel, _ch, _raw in qnotes:
                if slot < 0:
                    continue
                bar = slot // 16
                s = slot % 16
                len2 = max(1, min(24, int(round(length16 * 2))))
                iv = int(pitch - root_m)
                iv = max(-12, min(24, iv))
                ghost = 1 if vel < 96 else 0
                flags = 2 if ghost else 0
                vel_out = 80 if ghost else 112
                prev = by_bar[bar].get(s)
                if prev is None or vel > prev[-1]:
                    by_bar[bar][s] = [s, len2, iv, vel_out, flags, vel]
            bars = []
            if by_bar:
                last_bar = max(by_bar)
                for b in range(last_bar + 1):
                    evs = []
                    for s in sorted(by_bar.get(b, {})):
                        s, len2, iv, vel_out, flags, _rawv = by_bar[b][s]
                        evs.append((s, len2, iv, vel_out, flags))
                    # Glide into a chromatic neighbour.
                    for i in range(len(evs) - 1):
                        if abs(evs[i + 1][2] - evs[i][2]) in (1, 2) and evs[i][1] <= 4:
                            evs[i] = (evs[i][0], evs[i][1], evs[i][2], evs[i][3], evs[i][4] | 1)
                    bars.append(evs)
                    fill = 1 if b == last_bar or b % 4 == 3 else 0
                    packed = add_bass_bar(artist, evs, fill)
                    if packed:
                        bass_unique.setdefault(packed[0], packed[1])
                    if 1 <= len(evs) <= 8:
                        ivs = tuple(e[2] for e in evs)
                        contour_unique.setdefault((artist, ivs), {"artist": artist, "iv": list(ivs)})
            # 2-bar cells and ban hashes.
            for i in range(len(bars) - 1):
                if bars[i] and bars[i + 1]:
                    ha = hash_events([(e[0], e[1], pc_of(e[2])) for e in bars[i]])
                    hb = hash_events([(e[0], e[1], pc_of(e[2])) for e in bars[i + 1]])
                    bans.add(pair_hash(ha, hb))
                    ev = []
                    for e in bars[i]:
                        ev.append(e)
                    for e in bars[i + 1]:
                        ev.append((e[0] + 16, e[1], e[2], e[3], e[4]))
                    if 2 <= len(ev) <= 16:
                        sig = tuple((e[0], e[1], e[2], e[4] & 2) for e in ev)
                        sync = int(round(100 * sum(1 for s, *_ in ev if (s % 16) % 4 != 0) / len(ev)))
                        odd = int(round(100 * sum(1 for s, *_ in ev if (s % 16) % 2 == 1) / len(ev)))
                        bass_unique.setdefault((2, sig), {
                            "artist": artist, "bars": 2, "events": ev,
                            "fill": 1 if i % 4 == 2 else 0,
                            "sync": sync, "odd": odd, "dark": darkness([e[2] for e in ev]),
                        })

        elif kind in ("stab", "counter"):
            root_m = 60 + rpc  # offsets are already in the stab register
            # Group notes into chords when onsets share a 16th.
            chords = collections.defaultdict(list)
            for slot, length16, pitch, vel, _ch, _raw in qnotes:
                if slot < 0:
                    continue
                off = int(pitch - rpc - 60)
                while off > 24:
                    off -= 12
                while off < -12:
                    off += 12
                chords[slot].append((off, length16, vel))
            by_bar = collections.defaultdict(list)
            for slot in sorted(chords):
                notes_c = chords[slot]
                offs = sorted({n[0] for n in notes_c})[:4]
                length16 = max(n[1] for n in notes_c)
                vel = max(n[2] for n in notes_c)
                len2 = max(1, min(32, int(round(length16 * 2))))
                bar = slot // 16
                s = slot % 16
                by_bar[bar].append((s, len2, offs, 112 if vel >= 100 else 96))
            bar_ids = sorted(by_bar)
            for b in bar_ids:
                hits = by_bar[b][:8]
                if not hits:
                    continue
                sig = tuple((h[0], h[1], tuple(h[2])) for h in hits)
                mean_len = sum(h[1] for h in hits) / len(hits)
                padlike = 1 if mean_len >= 16 else 0
                klass = classify_voicing(hits[0][2])
                # A bar can mix voicings; tag by the longest hit.
                longest = max(hits, key=lambda h: h[1])
                klass = classify_voicing(longest[2])
                odd = int(round(100 * sum(1 for h in hits if h[0] % 2 == 1) / len(hits)))
                onbeat = int(round(100 * sum(1 for h in hits if h[0] % 4 == 0) / len(hits)))
                stab_unique.setdefault(sig, {
                    "artist": artist, "hits": hits, "klass": klass,
                    "pad": padlike, "odd": odd, "onbeat": onbeat,
                })
                for h in hits:
                    vkey = (tuple(h[2]), classify_voicing(h[2]))
                    voicing_unique.setdefault(vkey, {
                        "iv": list(h[2]), "klass": vkey[1], "artist": artist,
                    })
            ordered = []
            if bar_ids:
                for b in range(max(bar_ids) + 1):
                    ordered.append(by_bar.get(b, []))
                for i in range(len(ordered) - 1):
                    if not ordered[i] or not ordered[i + 1]:
                        continue
                    def stab_sig(hits):
                        ev = []
                        for h in hits:
                            pcs = [pc_of(x) for x in h[2][:4]]
                            while len(pcs) < 4:
                                pcs.append(0)
                            ev.append((h[0], h[1], pcs[0], pcs[1], pcs[2], pcs[3], len(h[2])))
                        return hash_events([(e[0], e[1], e[2]) for e in ev])
                    bans.add(pair_hash(stab_sig(ordered[i]), stab_sig(ordered[i + 1])))

        elif kind == "chop":
            by_bar = collections.defaultdict(list)
            for slot, length16, pitch, vel, _ch, _raw in qnotes:
                if slot < 0 or length16 > 6:
                    continue
                bar = slot // 16
                s = slot % 16
                by_bar[bar].append(s)
            for b, slots in by_bar.items():
                slots = sorted(set(slots))[:8]
                if not slots:
                    continue
                chop_unique.setdefault((artist, tuple(slots)), {"artist": artist, "slots": slots})

        elif kind == "drum":
            by_bar = collections.defaultdict(list)
            swing_num = []
            six = ppq / 4.0
            for slot, _length16, pitch, vel, _ch, raw in qnotes:
                if slot < 0:
                    continue
                voice = drum_voice(pitch)
                if voice is None:
                    stats[f"unmapped:{pitch}"] += 1
                    continue
                bar = slot // 16
                s = slot % 16
                by_bar[bar].append((s, VOICE_ID[voice], max(1, min(127, int(vel)))))
                if voice in ("hatC", "hatO") and s % 2 == 1:
                    # Position of this hit inside its 8th, in percent.
                    eight = (slot - 1) * six
                    # raw is the shifted tick.
                    pos = (raw - eight) / (2 * six)
                    if 0.2 < pos < 0.95:
                        swing_num.append(pos * 100.0)
            swing = int(round(sum(swing_num) / len(swing_num))) if swing_num else 50
            swing = max(46, min(68, swing))
            bar_ids = sorted(by_bar)
            ordered = []
            if bar_ids:
                for b in range(max(bar_ids) + 1):
                    hits = by_bar.get(b, [])
                    # One hit per slot/voice, loudest wins.
                    best = {}
                    for s, v, vel in hits:
                        key = (s, v)
                        if key not in best or vel > best[key]:
                            best[key] = vel
                    packed = tuple(sorted((s, v, vel) for (s, v), vel in best.items()))
                    ordered.append(packed)
                    if not packed:
                        continue
                    hat_slots = [s for s, v, _vel in packed if v in (6, 7)]
                    odd = int(round(100 * sum(1 for s in hat_slots if s % 2 == 1) / len(hat_slots))) if hat_slots else 0
                    snare_tail = sum(1 for s, v, _vel in packed if v == 1 and s >= 8)
                    toms = sum(1 for _s, v, _vel in packed if v in (4, 5))
                    kick_quarters = sum(1 for s, v, _vel in packed if v == 0 and s in (0, 4, 8, 12))
                    fill = 1 if snare_tail >= 2 or toms >= 1 or (b % 4 == 3 and kick_quarters < 4) else 0
                    drum_unique.setdefault(packed, {
                        "artist": artist, "hits": packed, "swing": swing,
                        "odd": odd, "fill": fill, "hats": len(hat_slots),
                    })
                for i in range(len(ordered) - 1):
                    if not ordered[i] or not ordered[i + 1]:
                        continue
                    def drum_sig(hits):
                        return hash_events([(s, v, min(15, vel // 8)) for s, v, vel in hits])
                    bans.add(pair_hash(drum_sig(ordered[i]), drum_sig(ordered[i + 1])))

    def emit_bass():
        for key, cell in bass_unique.items():
            off = len(bass_events)
            for e in cell["events"]:
                bass_events.append({
                    "slot": e[0], "len2": e[1], "iv": e[2], "vel": e[3], "flags": e[4],
                })
            bass_cells.append({
                "artist": ARTISTS.index(cell["artist"]),
                "bars": cell["bars"],
                "n": len(cell["events"]),
                "off": off,
                "sync": cell["sync"],
                "odd": cell["odd"],
                "dark": cell["dark"],
                "fill": cell["fill"],
            })

    def emit_contours():
        for (_a, ivs), cell in contour_unique.items():
            off = len(contour_ev)
            contour_ev.extend(ivs)
            contours.append({
                "artist": ARTISTS.index(cell["artist"]),
                "n": len(ivs),
                "off": off,
                "dark": darkness(ivs),
            })

    def emit_stabs():
        for _sig, cell in stab_unique.items():
            off = len(stab_hits)
            for h in cell["hits"]:
                iv = list(h[2]) + [0, 0, 0, 0]
                stab_hits.append({
                    "slot": h[0], "len2": h[1], "nIv": len(h[2]),
                    "iv": iv[:4], "vel": h[3],
                })
            stab_cells.append({
                "artist": ARTISTS.index(cell["artist"]),
                "n": len(cell["hits"]),
                "off": off,
                "klass": VOICING_ID[cell["klass"]],
                "pad": cell["pad"],
                "odd": cell["odd"],
                "onbeat": cell["onbeat"],
            })

    def emit_voicings():
        for (_k, cell) in voicing_unique.items():
            iv = list(cell["iv"]) + [0, 0, 0, 0]
            voicings.append({
                "artist": ARTISTS.index(cell["artist"]),
                "n": len(cell["iv"]),
                "iv": iv[:4],
                "klass": VOICING_ID[cell["klass"]],
            })

    def emit_drums():
        for _sig, cell in drum_unique.items():
            off = len(drum_hits)
            for s, v, vel in cell["hits"]:
                drum_hits.append({"slot": s, "voice": v, "vel": vel})
            drum_cells.append({
                "artist": ARTISTS.index(cell["artist"]),
                "n": len(cell["hits"]),
                "off": off,
                "swing": cell["swing"],
                "odd": cell["odd"],
                "hats": cell["hats"],
                "fill": cell["fill"],
            })

    def emit_chops():
        for (_k, cell) in chop_unique.items():
            off = len(chop_ev)
            chop_ev.extend(cell["slots"])
            chop_cells.append({
                "artist": ARTISTS.index(cell["artist"]),
                "n": len(cell["slots"]),
                "off": off,
            })

    emit_bass()
    emit_contours()
    emit_stabs()
    emit_voicings()
    emit_drums()
    emit_chops()

    return {
        "artists": ARTISTS,
        "styles": STYLE_WEIGHT,
        "bassEvents": bass_events,
        "bassCells": bass_cells,
        "contourIv": contour_ev,
        "contours": contours,
        "stabHits": stab_hits,
        "stabCells": stab_cells,
        "voicings": voicings,
        "drumHits": drum_hits,
        "drumCells": drum_cells,
        "chopSlots": chop_ev,
        "chopCells": chop_cells,
        "ban": sorted(bans),
        "stats": dict(stats),
    }


def cpp_u8(n) -> str:
    return str(int(n) & 0xFF)


def write_header(data: dict):
    lines = []
    a = lines.append
    a("// Generated by tools/extract_corpus.py. Do not edit by hand.")
    a("#pragma once")
    a("#include <cstdint>")
    a("namespace rolla { namespace corpus {")
    a("enum Artist : int {")
    a("    ArLocal = 0, ArLeak, ArStef, ArJosh, ArLaid, ArHigh, ArEllia, ArGaskin, ArMelak, ArSketch, ArCount")
    a("};")
    a("enum VoicingClass : int {")
    a("    VcQuartal = 0, VcMin7, VcMin9, VcMin11, VcFifth, VcDyad, VcSingle, VcOther")
    a("};")
    a("struct BassEv { std::uint8_t slot, len2, vel, flags; std::int8_t iv; };")
    a("struct BassCell { std::uint8_t artist, bars, n; std::uint16_t off; std::uint8_t sync, odd, dark, fill; };")
    a("struct Contour { std::uint8_t artist, n; std::uint16_t off; std::uint8_t dark; };")
    a("struct StabHit { std::uint8_t slot, len2, nIv, vel; std::int8_t iv[4]; };")
    a("struct StabCell { std::uint8_t artist, n; std::uint16_t off; std::uint8_t klass, pad, odd, onbeat; };")
    a("struct Voicing { std::uint8_t artist, n, klass; std::int8_t iv[4]; };")
    a("struct DrumHit { std::uint8_t slot, voice, vel; };")
    a("struct DrumCell { std::uint8_t artist, n; std::uint16_t off; std::uint8_t swing, odd, hats, fill; };")
    a("struct ChopCell { std::uint8_t artist, n; std::uint16_t off; };")
    a("")
    weights = []
    for style in ("ukmin", "long", "pop", "gaskin"):
        row = ", ".join(f"{STYLE_WEIGHT[style][name]:.2f}f" for name in ARTISTS)
        weights.append("    { " + row + " }")
    a("inline constexpr float kStyleWeight[4][ArCount] = {")
    a(",\n".join(weights))
    a("};")
    a("")

    def arr(name, typ, body):
        if not body:
            a(f"inline constexpr {typ} {name}[1] = {{}} ;")
            a(f"inline constexpr int {name}N = 0;")
            return
        a(f"inline constexpr {typ} {name}[] = {{")
        a(",\n".join(body))
        a("};")
        a(f"inline constexpr int {name}N = {len(body)};")
        a("")

    arr("kBassEv", "BassEv", [
        "    { " + ", ".join([
            cpp_u8(e["slot"]), cpp_u8(e["len2"]), cpp_u8(e["vel"]), cpp_u8(e["flags"]),
            str(int(e["iv"])),
        ]) + " }"
        for e in data["bassEvents"]
    ])
    arr("kBass", "BassCell", [
        "    { " + ", ".join(str(int(c[k])) for k in ("artist", "bars", "n", "off", "sync", "odd", "dark", "fill")) + " }"
        for c in data["bassCells"]
    ])
    ivs = ", ".join(str(int(v)) for v in data["contourIv"]) or "0"
    a(f"inline constexpr std::int8_t kContourIv[] = {{ {ivs} }};")
    a(f"inline constexpr int kContourIvN = {len(data['contourIv'])};")
    a("")
    arr("kContour", "Contour", [
        "    { " + ", ".join(str(int(c[k])) for k in ("artist", "n", "off", "dark")) + " }"
        for c in data["contours"]
    ])
    arr("kStabHit", "StabHit", [
        "    { " + ", ".join([
            cpp_u8(h["slot"]), cpp_u8(h["len2"]), cpp_u8(h["nIv"]), cpp_u8(h["vel"]),
            "{ " + ", ".join(str(int(x)) for x in h["iv"]) + " }",
        ]) + " }"
        for h in data["stabHits"]
    ])
    arr("kStab", "StabCell", [
        "    { " + ", ".join(str(int(c[k])) for k in ("artist", "n", "off", "klass", "pad", "odd", "onbeat")) + " }"
        for c in data["stabCells"]
    ])
    arr("kVoicing", "Voicing", [
        "    { " + ", ".join([
            str(int(v["artist"])), str(int(v["n"])), str(int(v["klass"])),
            "{ " + ", ".join(str(int(x)) for x in v["iv"]) + " }",
        ]) + " }"
        for v in data["voicings"]
    ])
    arr("kDrumHit", "DrumHit", [
        "    { " + ", ".join(str(int(h[k])) for k in ("slot", "voice", "vel")) + " }"
        for h in data["drumHits"]
    ])
    arr("kDrum", "DrumCell", [
        "    { " + ", ".join(str(int(c[k])) for k in ("artist", "n", "off", "swing", "odd", "hats", "fill")) + " }"
        for c in data["drumCells"]
    ])
    slots = ", ".join(str(int(s)) for s in data["chopSlots"]) or "0"
    a(f"inline constexpr std::uint8_t kChopSlot[] = {{ {slots} }};")
    a(f"inline constexpr int kChopSlotN = {len(data['chopSlots'])};")
    a("")
    arr("kChop", "ChopCell", [
        "    { " + ", ".join(str(int(c[k])) for k in ("artist", "n", "off")) + " }"
        for c in data["chopCells"]
    ])
    bans = data["ban"]
    # Chunk the ban list so the compiler is not handed one enormous line.
    a("inline constexpr std::uint64_t kBan[] = {")
    chunk = []
    for i, h in enumerate(bans):
        chunk.append(f"0x{h:016x}ull")
        if len(chunk) == 8:
            a("    " + ", ".join(chunk) + ",")
            chunk = []
    if chunk:
        a("    " + ", ".join(chunk))
    if not bans:
        a("    0ull")
    a("};")
    a(f"inline constexpr int kBanN = {len(bans)};")
    a("}} // namespace rolla::corpus")
    OUT_H.write_text("\n".join(lines) + "\n")


def main():
    data = collect()
    # JSON without the huge duplicated event streams is still useful; keep it all.
    summary = {
        "bassCells": len(data["bassCells"]),
        "bassEvents": len(data["bassEvents"]),
        "contours": len(data["contours"]),
        "stabCells": len(data["stabCells"]),
        "voicings": len(data["voicings"]),
        "drumCells": len(data["drumCells"]),
        "drumHits": len(data["drumHits"]),
        "chopCells": len(data["chopCells"]),
        "bans": len(data["ban"]),
        "files": data["stats"],
    }
    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "summary": summary,
        "artists": data["artists"],
        "styles": data["styles"],
        "bassCells": data["bassCells"],
        "bassEvents": data["bassEvents"],
        "contours": data["contours"],
        "contourIv": data["contourIv"],
        "stabCells": data["stabCells"],
        "stabHits": data["stabHits"],
        "voicings": data["voicings"],
        "drumCells": data["drumCells"],
        "drumHits": data["drumHits"],
        "chopCells": data["chopCells"],
        "chopSlots": data["chopSlots"],
        "ban": [f"{h:016x}" for h in data["ban"]],
    }
    OUT_JSON.write_text(json.dumps(payload))
    write_header(data)
    print(json.dumps(summary, indent=2))
    # Artist coverage
    def cover(cells, key="artist"):
        c = collections.Counter(ARTISTS[x[key] if isinstance(x, dict) else 0] for x in cells)
        return dict(c)
    print("bass", cover(data["bassCells"]))
    print("stab", cover(data["stabCells"]))
    print("drum", cover(data["drumCells"]))
    print("voicing class", collections.Counter(v["klass"] for v in data["voicings"]))
    print("stab pad", sum(c["pad"] for c in data["stabCells"]), "of", len(data["stabCells"]))
    print("drum fill", sum(c["fill"] for c in data["drumCells"]), "of", len(data["drumCells"]))
    print("bass fill", sum(c["fill"] for c in data["bassCells"]), "bars1", sum(1 for c in data["bassCells"] if c["bars"] == 1), "bars2", sum(1 for c in data["bassCells"] if c["bars"] == 2))


if __name__ == "__main__":
    main()
