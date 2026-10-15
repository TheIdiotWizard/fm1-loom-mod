#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# XY-1 in the browser: the host simulator compiled to WebAssembly (Emscripten), plus its page.
#   [XY_VERSION=0.1] web/emu/build.sh  ->  build/emu/{index.html, worklet.js, xy.wasm}
# Same sources and flags as host/build_host.sh (-ffp-contract=off, like the device).
set -e
cd "$(dirname "$0")/../.."
PY="${PYTHON:-python3}"
mkdir -p build/gen build/emu
[ -f build/gen/felucca_font.h ] || "$PY" tools/gen_font.py build/gen/felucca_font.h >/dev/null
[ -f build/gen/wavetables.h ] || "$PY" tools/gen_wavetables.py build/gen/wavetables.h >/dev/null
"$PY" tools/gen_words.py firmware/words.tsv web/words build/gen/ann_words.h >/dev/null
emcc -O2 -ffp-contract=off -std=gnu99 -Wall -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable \
    -DXY_HOST -DXY_WEB "-DXY_VERSION=\"$(printf %s "${XY_VERSION:-DEV}" | tr a-z A-Z)\"" \
    -Ifirmware/src -Ifirmware/src/seq -Ifirmware/src/dsp -Ibuild/gen \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/emu/xy.wasm web/emu/xy_web.c firmware/src/dsp/synth.c firmware/src/dsp/drumline.c firmware/src/dsp/fx.c \
    firmware/src/seq/pattern.c firmware/src/seq/seq.c
cp web/emu/index.html web/emu/worklet.js build/emu/
echo "emu: build/emu ($(wc -c < build/emu/xy.wasm) B wasm)"
