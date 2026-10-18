#!/usr/bin/env python3
"""rolla-rolls style names: --style takes roller|deep|bouncy|jackin and the old
ukmin|long|pop|gaskin ids in any case, folders keep the old ids by default, and
--folder-names styles names them after the styles. No samples needed.

  python3 tests/roll/style_alias_check.py --rolls ./build/rolla-rolls
"""

import argparse
import os
import subprocess
import sys
import tempfile

PAIRS = [("ROLLER", "ukmin", "roller"), ("deep", "long", "deep"), ("Bouncy", "pop", "bouncy"), ("JACKIN", "gaskin", "jackin"),
         ("UKMIN", "ukmin", "roller"), ("gaskin", "gaskin", "jackin")]


def run(rolls, out, style, extra=()):
    cmd = [rolls, "--style", style, "--count", "1", "--seed", "1", "--out", out, *extra]
    return subprocess.run(cmd, capture_output=True, text=True, timeout=600)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rolls", required=True)
    a = ap.parse_args()
    fails = 0

    def check(ok, what):
        nonlocal fails
        print(("PASS " if ok else "FAIL ") + what)
        fails += 0 if ok else 1

    for given, old_id, new_name in PAIRS:
        with tempfile.TemporaryDirectory() as d:
            p = run(a.rolls, d, given)
            check(p.returncode == 0 and os.path.isdir(os.path.join(d, old_id, "roll_00")),
                  f"--style {given}: default folder {old_id}/ (exit {p.returncode})")
        with tempfile.TemporaryDirectory() as d:
            p = run(a.rolls, d, given, ("--folder-names", "styles"))
            check(p.returncode == 0 and os.path.isdir(os.path.join(d, new_name, "roll_00"))
                  and not os.path.isdir(os.path.join(d, old_id)),
                  f"--style {given} --folder-names styles: folder {new_name}/")
    with tempfile.TemporaryDirectory() as d:
        check(run(a.rolls, d, "funk").returncode == 2, "unknown style is refused (exit 2)")
        check(run(a.rolls, d, "deep", ("--folder-names", "nope")).returncode == 2, "bad --folder-names is refused (exit 2)")
    print("style alias check passed" if fails == 0 else f"{fails} failure(s)")
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
