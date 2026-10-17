#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# LOOM-8 host tests: the maths, the platform pieces kept from Felucca / X0X, the units' own tests (the
# synth, the drumline, the FX, the sequencer: tests/host/*_test.sh, when present), storage, the update
# path, the host simulator, the browser build, and the whole firmware app in the simulator
# (tests/scenarios/*.xy: UI, patterns, persistence), with screenshots and audio in build/scenarios/.
# Silent: the audio is written to WAV files, never played.
#   tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
FAIL=0
OUT=build/host
mkdir -p "$OUT" build/scenarios build/gen
run() {
    name="$1"; shift
    if "$@" > "$OUT/$name.log" 2>&1; then
        echo "  ok   $name"
    else
        echo "  FAIL $name (see $OUT/$name.log)"
        tail -15 "$OUT/$name.log" | sed 's/^/       /'
        FAIL=1
    fi
}
[ -f build/gen/felucca_font.h ] || "$PY" tools/gen_font.py build/gen/felucca_font.h >/dev/null
[ -f build/gen/wavetables.h ] || "$PY" tools/gen_wavetables.py build/gen/wavetables.h >/dev/null
run fastmath sh -c "$CC -O2 -ffp-contract=off -Wall -Wextra -Werror -o $OUT/fastmath_test tests/host/fastmath_test.c -lm && $OUT/fastmath_test"   # its reference is double on purpose
run encoder sh -c "$CC -O2 -w -Ifirmware/hal -o $OUT/encoder_test tests/host/encoder_test.c && $OUT/encoder_test"
run uac sh -c "$CC -O2 -w -Ifirmware/src -Ifirmware/hal -o $OUT/uac_test tests/host/uac_test.c -lm && $OUT/uac_test"
run trs sh -c "$CC -O2 -w -Ifirmware/src -Ifirmware/hal -o $OUT/trs_test tests/host/trs_test.c && $OUT/trs_test"
run storage sh -c "$CC -O2 -o $OUT/storage_test tests/storage_test.c && $OUT/storage_test"
# the units' own tests, written beside them
for t in synth drumline fx seq; do
    if [ -f "tests/host/${t}_test.sh" ]; then
        run "$t" sh "tests/host/${t}_test.sh"
    else
        echo "  skip $t (no tests/host/${t}_test.sh)"
    fi
done
# the update path, against the firmware package (Felucca's tests; needs ./build.sh)
if [ -f build/loom.fwsc ]; then
    run ota-entry sh -c "$CC -o $OUT/ota_test tests/ota_test.c && $OUT/ota_test build/loom.fwsc"
    if [ -z "${AC79_SDK:-}" ]; then
        echo "  skip update-loader (needs AC79_SDK, as the build)"
    else
    run update-loader sh -c "head -c 200000 build/loom.bin > $OUT/old_app.bin && \
        $PY tools/fm1pkg_make.py $OUT/old_app.bin build/loader/ota.bin $OUT/old.fwsc >/dev/null && \
        $CC -o $OUT/ldr_test tests/ldr_test.c && $OUT/ldr_test $OUT/old.fwsc build/loom.fwsc"
    fi
    run installer "$PY" tests/install_test.py
else
    echo "  skip update-path tests (no build/loom.fwsc: run ./build.sh)"
fi
run host-build sh host/build_host.sh
if command -v emcc >/dev/null 2>&1 && command -v node >/dev/null 2>&1; then   # the browser build, when it can be built
    run emu sh -c "sh web/emu/build.sh >/dev/null && node tests/host/emu_test.mjs build/emu/xy.wasm"
fi
for s in tests/scenarios/*.xy; do
    [ -f "$s" ] || continue
    n=$(basename "$s" .xy)
    mkdir -p "build/scenarios/$n"
    run "scenario-$n" build/host/xy_host "$s" "build/scenarios/$n"
done
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
