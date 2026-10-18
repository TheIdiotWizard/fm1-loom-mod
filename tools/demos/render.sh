#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# LOOM-8's demo songs, rendered in the host simulator: each tools/demos/<song>.xy played from its song page, as a
# player would make it on the FM-1 (its own sample bank, its tracks, scenes, song, locks and throws), to a WAV, then
# AAC (.m4a). For private listening: the banks hold classic breaks and must never be committed, published or put on
# a site (they are made under build/demos/, git-ignored; the m4a files go to OUT). The mixdown is the simulator's
# output + 6 dB: the host writes what the FM-1's codec gets, 6 dB under its full scale (analyze.py --master).
#
#   tools/demos/render.sh [OUT] [SONG ...]     (OUT default build/demos/out; songs default all six)
#
# BREAKS: the breakbeat samples' folder (default ../../schwung-breakbeat/samples from fm1-xy, as fm1-parent has
# it). PYTHON: a Python with numpy and scipy (default /opt/homebrew/bin/python3.14, else python3). Builds the host
# simulator first (./build.sh --host). Prints each song's stats (voices, the modelled load) and the levels check.
set -eu
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
OUT="${1:-build/demos/out}"
[ $# -gt 0 ] && shift
SONGS="${*:-jungle garage hyperpop house pop hiphop}"
if [ -z "${PYTHON:-}" ]; then
    if [ -x /opt/homebrew/bin/python3.14 ]; then PYTHON=/opt/homebrew/bin/python3.14; else PYTHON=python3; fi
fi
export PYTHON
BREAKS="${BREAKS:-}"
if [ -z "$BREAKS" ]; then
    for d in ../../schwung-breakbeat/samples ../schwung-breakbeat/samples "$HOME/github/fm1-parent/schwung-breakbeat/samples"; do
        if [ -f "$d/amen01.wav" ]; then BREAKS="$d"; break; fi
    done
fi
if [ -z "$BREAKS" ] || [ ! -f "$BREAKS/amen01.wav" ]; then
    echo "render: no breakbeat samples found (set BREAKS to schwung-breakbeat's samples/ folder: amen01.wav, think.wav,"
    echo "        sesame.wav, funkydrummer.wav). They are not part of this repository and never will be."
    exit 1
fi
if ! "$PYTHON" -c "import numpy, scipy" 2>/dev/null; then
    echo "render: $PYTHON lacks numpy or scipy (set PYTHON to one that has them)"
    exit 1
fi
mkdir -p build/demos "$OUT"
sh build.sh --host >/dev/null || { echo "render: the host simulator did not build"; exit 1; }
"$PYTHON" tools/demos/make_banks.py "$BREAKS" build/demos $SONGS
for s in $SONGS; do
    d=build/demos/run_$s
    rm -rf "$d" && mkdir -p "$d"
    echo "== $s"
    rc=0
    app/build/host/loom_host "tools/demos/$s.xy" "$d" > "$d/log.txt" 2>&1 || rc=$?
    # the simulator's layout audit (gfx_loom.c): none of the songs' screens is flagged; a LAYOUT line is printed
    # below as a note, not a failure (the audio is still good)
    if [ $rc -ne 0 ] && { grep -qE '^(FAIL|demo: |line [0-9]+: unknown)' "$d/log.txt" || [ ! -f "$d/loom_demo_$s.wav" ]; }; then
        tail -20 "$d/log.txt"
        exit 1
    fi
    grep -E '^(stats|demo|LAYOUT)' "$d/log.txt" || true
    "$PYTHON" tools/demos/analyze.py --master "$d/loom_demo_$s.wav" "$d/mix_$s.wav"
    "$PYTHON" tools/demos/analyze.py "$d/mix_$s.wav"
    if command -v afconvert >/dev/null 2>&1; then
        afconvert -f m4af -d aac -b 128000 "$d/mix_$s.wav" "$OUT/loom_demo_$s.m4a"
    else
        ffmpeg -loglevel error -y -i "$d/mix_$s.wav" -c:a aac -b:a 128k "$OUT/loom_demo_$s.m4a"
    fi
    ls -l "$OUT/loom_demo_$s.m4a" | awk '{printf "   %s: %.2f MB\n", $NF, $5 / 1048576}'
done
