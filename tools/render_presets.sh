#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Audition the factory presets: each one plays a short phrase (three notes, then a chord held) on
# track 2 of a new project in the host simulator, through the default FX I delay and FX II reverb
# and the master, into build/presets/NN-engine-name.wav; then their levels (peak, RMS, the loudest
# 100 ms) into build/presets/levels.txt and a page to listen through, build/presets/index.html.
# Silent: nothing is played. Exits 1 when a render clips or is silent.
#   tools/render_presets.sh
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
OUT=build/presets
sh host/build_host.sh >/dev/null
mkdir -p "$OUT"
rm -f "$OUT"/*.wav "$OUT"/*.xy
$CC -O2 -std=c99 -DXY_HOST -Ifirmware/src/dsp -Ifirmware/src -Ibuild/gen -o build/host/preset_scripts \
    tools/preset_scripts.c firmware/src/dsp/synth.c firmware/src/dsp/drumline.c -lm
names=$(build/host/preset_scripts "$OUT")
for n in $names; do
    build/host/xy_host "$OUT/$n.xy" "$OUT" >/dev/null || { echo "render failed: $n"; exit 1; }
done
"$PY" tools/preset_levels.py "$OUT" $names
echo "presets: $OUT/index.html, $OUT/levels.txt"
