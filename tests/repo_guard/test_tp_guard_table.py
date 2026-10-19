#!/usr/bin/env python3
"""Self-test for tools/tp_guard_table.py on fabricated sidecars (no audio)."""

import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "tools", "tp_guard_table.py")


def write(root, rel, data):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(data if isinstance(data, str) else json.dumps(data))


def run(*args):
    p = subprocess.run([sys.executable, TOOL, *args], capture_output=True, text=True)
    return p.returncode, p.stdout


def main():
    fails = 0
    root = tempfile.mkdtemp(prefix="tpg-table-")
    write(root, "UKMIN/FULL.wav.gr.json", {"tp_guard_gr_db_max": 0.0})
    write(root, "LONG/FULL.wav.gr.json", {"tp_guard_gr_db_max": 0.25})
    write(root, "POP/FULL.wav.gr.json", {"tp_guard_gr_db_max": 0.7})
    write(root, "POP/MUTE_BASS.wav.gr.json", {"tp_guard_gr_db_max": 0.1})
    write(root, "JACKIN/FULL.wav.gr.json", {"gr_db_max": 0.0})   # older sidecar, no field
    write(root, "rolls/jackin/roll_03.wav.gr.json", "not json")  # unreadable

    code, out = run(root, "--json", "--bar", "1.0")
    t = {r["style"]: r for r in json.loads(out)["styles"]}
    checks = [
        (code == 0, "exit 0 without --gate"),
        (t["ROLLER"]["max_gr_db"] == 0.0 and t["DEEP"]["max_gr_db"] == 0.25, "per-style max"),
        (t["BOUNCY"]["renders"] == 2 and t["BOUNCY"]["max_gr_db"] == 0.7 and t["BOUNCY"]["mean_gr_db"] == 0.4, "BOUNCY max / mean"),
        (t["BOUNCY"]["worst"].endswith("FULL.wav"), "worst render named"),
        (t["JACKIN"]["renders"] == 2 and t["JACKIN"]["with_field"] == 0 and t["JACKIN"]["max_gr_db"] is None, "missing field counted, not failed"),
    ]
    code, _ = run(root, "--gate", "--bar", "1.0")
    checks.append((code == 0, "--gate passes under the bar"))
    write(root, "POP/COMBO_11.wav.gr.json", {"tp_guard_gr_db_max": 1.4})
    code, out = run(root, "--gate", "--bar", "1.0")
    checks.append((code == 1 and "| BOUNCY | 3 | 3 | 1.40 |" in out, "--gate fails over the bar, table shows it"))
    code, out = run(tempfile.mkdtemp(prefix="tpg-empty-"))
    checks.append((code == 0 and "no .gr.json sidecars" in out, "empty folder is not an error"))

    for ok, what in checks:
        print(("PASS " if ok else "FAIL ") + what)
        fails += 0 if ok else 1
    print("tp_guard table self-test passed" if fails == 0 else f"{fails} failure(s)")
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
