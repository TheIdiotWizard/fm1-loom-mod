#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build and run the drumline's host test (tests/host/drumline_test.c): WAVs in build/host/drum_*.wav.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p build/host
${CC:-cc} -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DXY_HOST -Ifirmware/src/dsp \
    -o build/host/drumline_test tests/host/drumline_test.c firmware/src/dsp/drumline.c
build/host/drumline_test
