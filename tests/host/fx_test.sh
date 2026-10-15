#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build and run the effects' host test (tests/host/fx_test.c): WAVs in build/host/fx_*.wav.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p build/host
${CC:-cc} -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DXY_HOST -Ifirmware/src/dsp \
    -o build/host/fx_test tests/host/fx_test.c firmware/src/dsp/fx.c
build/host/fx_test
