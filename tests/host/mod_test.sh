#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The modulation matrix's host test: dsp/synth.c with the device's rules (C99, float only, no fused multiply-add),
# the matrix driven directly (tests/host/mod_test.c).
#   tests/host/mod_test.sh
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
mkdir -p build/gen build/host
"$PY" tools/gen_wavetables.py build/gen/wavetables.h
[ -f build/gen/samples.h ] || "$PY" tools/gen_samples.py build/gen/samples.h >/dev/null
"$PY" tools/gen_presets.py build/gen/presets.h
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DPLAT_HOST -Iapp/dsp -Ibase/include -Ibuild/gen \
    -o build/host/mod_test tests/host/mod_test.c app/dsp/synth.c app/dsp/drumline.c app/dsp/sample.c -lm
build/host/mod_test
