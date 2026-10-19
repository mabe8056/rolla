#!/usr/bin/env python3
"""Per-style table of the master tp_guard's gain reduction (MIX_SPEC §9.4b).

Reads every <render>.wav.gr.json sidecar under the given folders (what
rolla-mix-render writes next to its renders) and prints, per style, how many
renders there are, how many carry tp_guard_gr_db_max, the peak and mean GR, the
worst render, and how many exceed the 1 dB factory bar. Sidecars without the
field are counted, not failed.

  python3 tools/tp_guard_table.py DIR [DIR ...] [--json] [--gate]

--gate exits 1 if any render is over the bar (default 1.0 dB, or the
mix_profiles.json tp_guard.max_gr_db_factory value when the repo has it).
"""

import argparse
import glob
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STYLE_OF = {
    "ukmin": "ROLLER", "uk_min": "ROLLER", "roller": "ROLLER",
    "long": "DEEP", "deep": "DEEP",
    "pop": "BOUNCY", "bouncy": "BOUNCY",
    "jackin": "JACKIN",
}
ORDER = ["ROLLER", "DEEP", "BOUNCY", "JACKIN", "?"]
FIELD = "tp_guard_gr_db_max"


def factory_bar():
    try:
        with open(os.path.join(ROOT, "resources", "mix_profiles.json"), encoding="utf-8") as f:
            return float(json.load(f)["tp_guard"]["max_gr_db_factory"])
    except (OSError, KeyError, ValueError, TypeError):
        return 1.0


def style_for(path, base):
    rel = os.path.relpath(path, base)
    for part in reversed(rel.replace("\\", "/").split("/")[:-1]):
        s = STYLE_OF.get(part.lower())
        if s:
            return s
    name = os.path.basename(path).lower()
    for key, s in STYLE_OF.items():
        if name.startswith(key + "_") or ("_" + key + "_") in name:
            return s
    return "?"


def collect(dirs):
    rows = []
    for base in dirs:
        for path in sorted(glob.glob(os.path.join(base, "**", "*.gr.json"), recursive=True)):
            try:
                with open(path, encoding="utf-8") as f:
                    data = json.load(f)
            except (OSError, ValueError):
                data = None
            gr = data.get(FIELD) if isinstance(data, dict) else None
            rows.append({"style": style_for(path, base), "render": os.path.relpath(path, base)[: -len(".gr.json")],
                         "gr": float(gr) if isinstance(gr, (int, float)) else None})
    return rows


def table(rows, bar):
    out = []
    for style in ORDER:
        rs = [r for r in rows if r["style"] == style]
        if not rs:
            continue
        have = [r for r in rs if r["gr"] is not None]
        worst = max(have, key=lambda r: r["gr"]) if have else None
        out.append({
            "style": style,
            "renders": len(rs),
            "with_field": len(have),
            "max_gr_db": round(worst["gr"], 3) if worst else None,
            "mean_gr_db": round(sum(r["gr"] for r in have) / len(have), 3) if have else None,
            "worst": worst["render"] if worst else None,
            "over_bar": sum(1 for r in have if r["gr"] > bar + 1e-9),
        })
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--gate", action="store_true")
    ap.add_argument("--bar", type=float, default=None)
    a = ap.parse_args(argv)
    bar = a.bar if a.bar is not None else factory_bar()
    rows = collect(a.dirs)
    t = table(rows, bar)
    over = sum(r["over_bar"] for r in t)
    if a.json:
        print(json.dumps({"bar_db": bar, "styles": t, "renders": len(rows), "over_bar": over}, indent=1))
    else:
        if not rows:
            print("no .gr.json sidecars found")
        else:
            print(f"tp_guard gain reduction (bar {bar:g} dB)\n")
            print("| Style | Renders | With GR field | Max GR dB | Mean GR dB | Over bar | Worst render |")
            print("| --- | --- | --- | --- | --- | --- | --- |")
            fmt = lambda v: "n/a" if v is None else f"{v:.2f}"
            for r in t:
                print(f"| {r['style']} | {r['renders']} | {r['with_field']} | {fmt(r['max_gr_db'])} | {fmt(r['mean_gr_db'])} "
                      f"| {r['over_bar']} | {r['worst'] or 'n/a'} |")
            missing = sum(1 for r in rows if r["gr"] is None)
            if missing:
                print(f"\n{missing} sidecar(s) without {FIELD} (older render, or a non-SUM file)")
    return 1 if (a.gate and over) else 0


if __name__ == "__main__":
    sys.exit(main())
