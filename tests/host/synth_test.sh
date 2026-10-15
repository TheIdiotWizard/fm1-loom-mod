#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The voices' host test: generate the wavetables, build dsp/synth.c with the device's rules (C99, float
# only, no libm, no fused multiply-add), run the checks and the benchmark.
#   tests/host/synth_test.sh
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
mkdir -p build/gen build/host
"$PY" tools/gen_wavetables.py build/gen/wavetables.h
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DXY_HOST -Ifirmware/src/dsp -Ibuild/gen \
    -o build/host/synth_test tests/host/synth_test.c firmware/src/dsp/synth.c
build/host/synth_test
