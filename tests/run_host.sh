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
W="-O2 -std=c99 -ffp-contract=off -Wall -Wextra -Werror -Wno-unused-function -Ibuild/gen/x0x -Ibuild/gen -Isrc/dsp -Isrc/app -Ix0x-firmware/firmware/src"
# the engines as the FM-1 builds them: one unit of every src/dsp/*.c (names must not clash)
for f in src/dsp/*.c; do echo "#include \"../../$f\""; done > build/host/dsp_unity.c
# shellcheck disable=SC2086
$CC $W -c build/host/dsp_unity.c -o build/host/dsp_unity.o
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
$CC $W -D_DEFAULT_SOURCE tests/host/fx_test.c src/dsp/dn_fx.c src/dsp/dn_fxc.c src/dsp/dn_fxq.c src/dsp/dn_fxm.c src/dsp/dn_voice.c -o build/host/fx_test -lm
build/host/fx_test
# shellcheck disable=SC2086
$CC $W tests/host/fx2_test.c src/dsp/dn_fx.c src/dsp/dn_fxc.c src/dsp/dn_fxq.c src/dsp/dn_fxm.c -o build/host/fx2_test -lm
build/host/fx2_test build/host tests/host/fx_golden.txt
# shellcheck disable=SC2086
$CC $W -D_DEFAULT_SOURCE tests/host/sub_test.c src/dsp/dn_sub.c -o build/host/sub_test -lm
build/host/sub_test build/host
# FM (0.25) against Dexed's own MSFA (tests/host/ref: built only here)
CXX=${CXX:-c++}
mkdir -p build/host/ref
for f in env exp2 sin freqlut lfo pitchenv fm_core fm_op_kernel dx7note; do
    $CXX -O2 -std=c++17 -w -c tests/host/ref/msfa/$f.cc -o build/host/ref/$f.o -Itests/host/ref/msfa
done
$CXX -O2 -std=c++17 -w -c tests/host/ref/msfa/porta.cpp -o build/host/ref/porta.o -Itests/host/ref/msfa
$CXX -O2 -std=c++17 -w -c tests/host/ref/ref_fm.cc -o build/host/ref/ref_fm.o -Itests/host/ref -Itests/host/ref/msfa
# shellcheck disable=SC2086
$CC $W -D_DEFAULT_SOURCE -c tests/host/fm_test.c -o build/host/fm_test.o
# shellcheck disable=SC2086
$CC $W -c src/dsp/dn_fm.c -o build/host/dn_fm.o
# shellcheck disable=SC2086
$CC $W -c src/dsp/reese.c -o build/host/reese_fm.o
$CXX build/host/fm_test.o build/host/dn_fm.o build/host/reese_fm.o build/host/ref/*.o -o build/host/fm_test -lm
build/host/fm_test build/host tests/host/fm_golden.txt
# shellcheck disable=SC2086
$CC $W -D_DEFAULT_SOURCE tests/host/smp_test.c src/dsp/dn_smp.c -o build/host/smp_test -lm
build/host/smp_test
# (0.29) X0X's storage.c as built (tools/dn_patches.py): the settings objects in slot 3's top 16 KB
python3 tools/dn_patches.py storage x0x-firmware/firmware/src build/gen/storage_dn.c
# shellcheck disable=SC2086
$CC $W -Ix0x-firmware/firmware/src/app tests/host/store_test.c -o build/host/store_test
build/host/store_test
# shellcheck disable=SC2086
$CC $W tests/host/slices_xcheck.c src/dsp/dn_breaks.c -o build/host/slices_xcheck
build/host/slices_xcheck | node tests/host/slices_xcheck.mjs
# shellcheck disable=SC2086
$CC $W -Wno-unused-parameter -Wno-sign-compare tests/host/dn_host.c src/dsp/reese.c src/dsp/dn_seq.c src/dsp/dn_breaks.c src/dsp/dn_lfo.c src/dsp/dn_fx.c src/dsp/dn_fxc.c src/dsp/dn_fxq.c src/dsp/dn_fxm.c src/dsp/dn_sub.c src/dsp/dn_smp.c src/dsp/dn_fm.c -o build/host/dn_host
for s in tests/host/scenarios/*.txt; do
    build/host/dn_host "$s" build/host
done
node tests/host/editor_golden.mjs build/host/editor-golden.json
echo "host tests: all ok"
