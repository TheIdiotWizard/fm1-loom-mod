#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The two cores' split of the voice pool on the host (tests/host/split_bench.c): the critical path with
# the old fixed split and with app/engine.c split_at's.
set -eu
cd "$(dirname "$0")/../.."
PY="${PYTHON:-python3}"
mkdir -p build/gen build/host
[ -f build/gen/wavetables.h ] || "$PY" tools/gen_wavetables.py build/gen/wavetables.h
[ -f build/gen/samples.h ] || "$PY" tools/gen_samples.py build/gen/samples.h >/dev/null
${CC:-cc} -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DXY_HOST -Ifirmware/src/dsp -Ibuild/gen \
    -o build/host/split_bench tests/host/split_bench.c firmware/src/dsp/synth.c firmware/src/dsp/drumline.c \
    firmware/src/dsp/sample.c
build/host/split_bench
