#!/usr/bin/env bash
# Copies a sample pack into a built ROLLA bundle, where the plugin looks first:
#   <bundle>/Contents/Resources/Samples
# then writes Samples/.rolla-pack.json (file count, manifest hash) and, on macOS,
# re-signs the bundle ad hoc so hosts still load it.
#
#   tools/inject_pack.sh build/ROLLA_artefacts/Release/VST3/ROLLA.vst3 /path/to/pack
#   tools/inject_pack.sh build/ROLLA_artefacts/Release/AU/ROLLA.component /path/to/pack
#   tools/inject_pack.sh <bundle> /path/to/pack /path/to/private/genre    # + Resources/Genre
#
# The optional third argument (or $ROLLA_GENRE_DATA_DIR) is the private genre
# data folder: orig_builtin.json (the originality backstop's banned figures,
# hashed) and genre_targets.json go into <bundle>/Contents/Resources/Genre.
# They are private and never committed; the plugin reads them from the bundle.
#
# The pack stays out of the repo: this only writes inside the bundle.
# cmake -DROLLA_SAMPLE_PACK_DIR=/path/to/pack runs this after every plugin build.
set -euo pipefail

usage="usage: tools/inject_pack.sh <ROLLA.vst3|ROLLA.component|ROLLA.app> <packdir> [genre_data_dir]"
bundle=${1:?$usage}
pack=${2:?$usage}
bundle=${bundle%/}
pack=${pack%/}

if [ ! -d "$bundle/Contents" ]; then
    echo "inject_pack: $bundle is not a plugin bundle (no Contents folder)" >&2
    exit 1
fi
if [ ! -d "$pack" ]; then
    echo "inject_pack: $pack is not a folder" >&2
    exit 1
fi

count_audio() {
    find "$1" -type f \( -iname '*.wav' -o -iname '*.aif' -o -iname '*.aiff' -o -iname '*.flac' \) | wc -l | tr -d ' '
}

source_files=$(count_audio "$pack")
if [ "$source_files" -eq 0 ]; then
    echo "inject_pack: $pack holds no .wav/.aif/.aiff/.flac files" >&2
    exit 1
fi

dest="$bundle/Contents/Resources/Samples"
rm -rf "$dest"
mkdir -p "$dest"
if [ "$(uname)" = "Darwin" ]; then
    # APFS clones: no second copy on disk. Plain copy across volumes.
    cp -cR "$pack/." "$dest/" 2>/dev/null || cp -R "$pack/." "$dest/"
    # Finder info and quarantine flags make codesign refuse the bundle.
    xattr -cr "$dest" 2>/dev/null || true
else
    cp -R "$pack/." "$dest/"
fi

files=$(count_audio "$dest")
manifest=""
for name in SAMPLE_MANIFEST_v2.json SAMPLE_MANIFEST.json; do
    if [ -f "$dest/$name" ]; then
        manifest=$name
        break
    fi
done
manifest_sha=""
if [ -n "$manifest" ]; then
    if command -v shasum >/dev/null 2>&1; then
        manifest_sha=$(shasum -a 256 "$dest/$manifest" | cut -d' ' -f1)
    else
        manifest_sha=$(sha256sum "$dest/$manifest" | cut -d' ' -f1)
    fi
fi

cat > "$dest/.rolla-pack.json" <<EOF
{
  "files": $files,
  "manifest": "${manifest}",
  "manifestSha256": "${manifest_sha}"
}
EOF

genre=${3:-${ROLLA_GENRE_DATA_DIR:-}}
genre_files=0
if [ -n "$genre" ]; then
    if [ ! -d "$genre" ]; then
        echo "inject_pack: genre data dir $genre is not a folder" >&2
        exit 1
    fi
    gdest="$bundle/Contents/Resources/Genre"
    rm -rf "$gdest"
    mkdir -p "$gdest"
    for f in orig_builtin.json genre_targets.json; do
        if [ -f "$genre/$f" ]; then
            cp "$genre/$f" "$gdest/$f"
            genre_files=$((genre_files + 1))
        fi
    done
    [ "$(uname)" = "Darwin" ] && xattr -cr "$gdest" 2>/dev/null || true
fi

if [ "$(uname)" = "Darwin" ]; then
    codesign --force --deep -s - "$bundle"
fi

echo "inject_pack: $files audio files -> $dest${manifest:+ ($manifest $manifest_sha)}"
