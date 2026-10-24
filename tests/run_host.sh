#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# DREADNOUGHT's host tests: the engines alone, measured against the maths, then the whole app driven from the
# panel (tests/host/scenarios). Screens and sounds land in build/host. Needs cc, python3 (numpy for the measures).
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
mkdir -p build/gen build/host
python3 tools/gen_dn_tables.py build/gen/dn_tables.h >/dev/null
python3 tools/gen_dn_breaks.py build/gen/dn_breaks_builtin.h --wav build/host >/dev/null
mkdir -p build/gen/x0x
[ -f build/gen/x0x/felucca_font.h ] || python3 x0x-firmware/tools/gen_font.py build/gen/x0x/felucca_font.h >/dev/null
W="-O2 -std=c99 -ffp-contract=off -Wall -Wextra -Werror -Wno-unused-function -Ibuild/gen/x0x -Ibuild/gen -Isrc/dsp -Isrc/app"
# shellcheck disable=SC2086
$CC $W tests/host/reese_test.c src/dsp/reese.c -o build/host/reese_test
build/host/reese_test build/host
python3 tests/host/reese_measure.py
# shellcheck disable=SC2086
$CC $W tests/host/seq_test.c src/dsp/dn_seq.c -o build/host/seq_test -lm
build/host/seq_test
# shellcheck disable=SC2086
$CC $W tests/host/brk_test.c src/dsp/dn_breaks.c -o build/host/brk_test -lm
build/host/brk_test build/host
# shellcheck disable=SC2086
$CC $W -D_DEFAULT_SOURCE tests/host/lfo_test.c src/dsp/dn_lfo.c src/dsp/dn_seq.c src/dsp/reese.c src/dsp/dn_breaks.c -o build/host/lfo_test -lm
build/host/lfo_test
# shellcheck disable=SC2086
$CC $W -D_DEFAULT_SOURCE tests/host/fx_test.c src/dsp/dn_fx.c src/dsp/dn_voice.c -o build/host/fx_test -lm
build/host/fx_test
# shellcheck disable=SC2086
$CC $W -D_DEFAULT_SOURCE tests/host/sub_test.c src/dsp/dn_sub.c -o build/host/sub_test -lm
build/host/sub_test build/host
# shellcheck disable=SC2086
$CC $W tests/host/slices_xcheck.c src/dsp/dn_breaks.c -o build/host/slices_xcheck
build/host/slices_xcheck | node tests/host/slices_xcheck.mjs
# shellcheck disable=SC2086
$CC $W -Wno-unused-parameter -Wno-sign-compare tests/host/dn_host.c src/dsp/reese.c src/dsp/dn_seq.c src/dsp/dn_breaks.c src/dsp/dn_lfo.c src/dsp/dn_fx.c src/dsp/dn_sub.c -o build/host/dn_host
for s in tests/host/scenarios/*.txt; do
    build/host/dn_host "$s" build/host
done
node tests/host/editor_golden.mjs build/host/editor-golden.json
echo "host tests: all ok"
