#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the LOOM-8 host simulator (the whole app on this machine) into build/host/xy_host.
# Same sources as the firmware, -ffp-contract=off like the device (no fused multiply-add); the DSP
# units are compiled as on the device (separate units), the rest is the unity build. No libm but
# the WAV writer's (fastmath.h everywhere else). -Wdouble-promotion -Werror: no double in the app.
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
mkdir -p build/gen build/host
[ -f build/gen/felucca_font.h ] || "$PY" tools/gen_font.py build/gen/felucca_font.h >/dev/null
[ -f build/gen/wavetables.h ] || "$PY" tools/gen_wavetables.py build/gen/wavetables.h >/dev/null
[ -f build/gen/samples.h ] || "$PY" tools/gen_samples.py build/gen/samples.h >/dev/null
"$PY" tools/gen_words.py firmware/words.tsv web/words build/gen/ann_words.h >/dev/null
W="-O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -Wno-unused-function -Wno-unused-parameter"
I="-DXY_HOST -Ifirmware/src -Ifirmware/src/seq -Ifirmware/src/dsp -Ibuild/gen"
for u in dsp/synth dsp/drumline dsp/sample dsp/fx seq/pattern seq/seq; do
    $CC $W $I -c firmware/src/$u.c -o build/host/$(basename $u).o
done
$CC $W $I -Wno-unused-variable -o build/host/xy_host host/xy_host.c build/host/synth.o build/host/drumline.o \
    build/host/sample.o build/host/fx.o build/host/pattern.o build/host/seq.o -lm
echo "host: build/host/xy_host"
