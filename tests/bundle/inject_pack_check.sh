#!/usr/bin/env bash
# Checks tools/inject_pack.sh on a copy of a built bundle (the original is not touched):
# the pack lands in Contents/Resources/Samples, .rolla-pack.json has the right file
# count and manifest hash, and on macOS the bundle still verifies after the ad-hoc re-sign.
#
#   tests/bundle/inject_pack_check.sh build/ROLLA_artefacts/Release/VST3/ROLLA.vst3
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
src=${1:?usage: inject_pack_check.sh <built ROLLA.vst3|ROLLA.component>}
src=${src%/}
work=$(mktemp -d "${TMPDIR:-/tmp}/rolla-inject-check-XXXXXX")
trap 'rm -rf "$work"' EXIT

fails=0
check() {
    if [ "$1" = 0 ]; then echo "PASS $2"; else echo "FAIL $2"; fails=$((fails + 1)); fi
}

touch "$work/.start"
bundle="$work/$(basename "$src")"
cp -R "$src" "$bundle"
pack="$work/pack"
python3 - "$pack" <<'EOF'
import math, os, struct, sys, wave
root = sys.argv[1]
for rel in ["BASS/b_D1.wav", "STAB/s_D4.wav", "VOX/chops/c_Fs4.wav", "DRUMS/KICK/k.wav", "DRUMS/CH/h.wav"]:
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(48000)
        w.writeframes(b"".join(struct.pack("<h", int(8000 * math.sin(i / 10.0))) for i in range(4800)))
with open(os.path.join(root, "SAMPLE_MANIFEST_v2.json"), "w") as f:
    f.write('{"count": 0, "files": []}\n')
with open(os.path.join(root, "README.txt"), "w") as f:
    f.write("not audio\n")
EOF

out=$("$repo/tools/inject_pack.sh" "$bundle" "$pack")
echo "$out"
dest="$bundle/Contents/Resources/Samples"
check $([ -f "$dest/BASS/b_D1.wav" ] && [ -f "$dest/DRUMS/KICK/k.wav" ] && echo 0 || echo 1) "pack copied into Contents/Resources/Samples"
check $([ -f "$dest/.rolla-pack.json" ] && echo 0 || echo 1) ".rolla-pack.json written"
check $(grep -q '"files": 5' "$dest/.rolla-pack.json" && echo 0 || echo 1) ".rolla-pack.json counts 5 audio files"
if command -v shasum >/dev/null 2>&1; then want=$(shasum -a 256 "$pack/SAMPLE_MANIFEST_v2.json" | cut -d' ' -f1)
else want=$(sha256sum "$pack/SAMPLE_MANIFEST_v2.json" | cut -d' ' -f1); fi
check $(grep -q "\"manifestSha256\": \"$want\"" "$dest/.rolla-pack.json" && echo 0 || echo 1) ".rolla-pack.json has the manifest hash"
check $([ -z "$(find "$src" -newer "$work/.start" -print -quit)" ] && echo 0 || echo 1) "the source bundle is untouched"

# Running it again replaces the pack (no stale files from the previous one).
rm "$pack/STAB/s_D4.wav"
"$repo/tools/inject_pack.sh" "$bundle" "$pack" >/dev/null
check $([ ! -e "$dest/STAB/s_D4.wav" ] && grep -q '"files": 4' "$dest/.rolla-pack.json" && echo 0 || echo 1) "a second inject replaces the old pack"

if [ "$(uname)" = "Darwin" ]; then
    if codesign --verify --deep --strict "$bundle" 2>"$work/verify.txt"; then
        check 0 "codesign --verify --deep --strict after the ad-hoc re-sign"
    else
        check 1 "codesign --verify --deep --strict after the ad-hoc re-sign"
        cat "$work/verify.txt"
    fi
fi

if ! "$repo/tools/inject_pack.sh" "$work/not-a-bundle" "$pack" 2>/dev/null; then check 0 "refuses a folder that is not a bundle"
else check 1 "refuses a folder that is not a bundle"; fi
mkdir -p "$work/empty"
if ! "$repo/tools/inject_pack.sh" "$bundle" "$work/empty" 2>/dev/null; then check 0 "refuses a pack with no audio"
else check 1 "refuses a pack with no audio"; fi

[ "$fails" = 0 ] && echo "inject_pack checks passed" || echo "$fails inject_pack check failure(s)"
exit "$fails"
