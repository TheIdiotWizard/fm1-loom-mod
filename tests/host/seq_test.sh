#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# XY-1 sequencer tests: build and run tests/host/seq_test.c against seq.c and pattern.c (no libm,
# no synth: the sequencer only needs synth.h's enums).
#   sh tests/host/seq_test.sh
set -eu
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
mkdir -p build/host
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DXY_HOST \
    -Ifirmware/src -Ifirmware/src/seq -Ifirmware/src/dsp \
    -o build/host/seq_test tests/host/seq_test.c firmware/src/seq/seq.c firmware/src/seq/pattern.c
build/host/seq_test
