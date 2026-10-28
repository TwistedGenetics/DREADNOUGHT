#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# DREADNOUGHT's host tests: the engines alone, measured against the maths, then the whole app driven from the
# panel (tests/host/scenarios). Screens and sounds land in build/host. Needs cc, python3 (numpy for the measures).
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
mkdir -p build/gen build/host
python3 tools/gen_dn_tables.py build/gen/dn_tables.h >/dev/null
python3 tools/gen_dn_wt.py build/gen/dn_wt_tables.h >/dev/null   # (0.32: the factory wavetables)
python3 tools/gen_dn_breaks.py build/gen/dn_breaks_builtin.h --wav build/host >/dev/null
[ -f build/gen/dn_font.h ] || python3 tools/gen_dn_font.py build/gen/dn_font.h   # (needs Pillow with libraqm)
W="-O2 -std=c99 -ffp-contract=off -Wall -Wextra -Werror -Wno-unused-function -Ibuild/gen -Isrc/core -Isrc/dsp -Isrc/app"
# the engines as the FM-1 builds them: one unit of every src/dsp/*.c (names must not clash)
for f in src/dsp/*.c; do echo "#include \"../../$f\""; done > build/host/dsp_unity.c
# shellcheck disable=SC2086
$CC $W -c build/host/dsp_unity.c -o build/host/dsp_unity.o
# shellcheck disable=SC2086
$CC $W tests/host/math_test.c -o build/host/math_test -lm
build/host/math_test
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
$CC $W -D_DEFAULT_SOURCE tests/host/fx_test.c src/dsp/dn_fx.c src/dsp/dn_fxc.c src/dsp/dn_fxq.c src/dsp/dn_fxm.c src/dsp/dn_fxl.c src/dsp/dn_voice.c -o build/host/fx_test -lm
build/host/fx_test
# (0.46) the SMK's smaller effects (DN_FX_SMALL: echoes half as long, tanks half the size): the same checks
# shellcheck disable=SC2086
$CC $W -D_DEFAULT_SOURCE -DDN_FX_SMALL tests/host/fx_test.c src/dsp/dn_fx.c src/dsp/dn_fxc.c src/dsp/dn_fxq.c src/dsp/dn_fxm.c src/dsp/dn_fxl.c src/dsp/dn_voice.c -o build/host/fx_test_small -lm
build/host/fx_test_small > build/host/fx_small.txt || { cat build/host/fx_small.txt; exit 1; }
tail -1 build/host/fx_small.txt
# shellcheck disable=SC2086
$CC $W tests/host/fx2_test.c src/dsp/dn_fx.c src/dsp/dn_fxc.c src/dsp/dn_fxq.c src/dsp/dn_fxm.c src/dsp/dn_fxl.c -o build/host/fx2_test -lm
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
# (0.32) WAVE, the wavetable synth
# shellcheck disable=SC2086
$CC $W -D_DEFAULT_SOURCE tests/host/wt_test.c src/dsp/dn_wt.c src/dsp/dn_fm.c src/dsp/reese.c -o build/host/wt_test -lm
build/host/wt_test build/host
# shellcheck disable=SC2086
$CC $W -D_DEFAULT_SOURCE tests/host/ju_test.c src/dsp/dn_ju.c src/dsp/dn_fm.c -o build/host/ju_test -lm
build/host/ju_test build/host
python3 tests/host/wt_xcheck.py build/host/wtx       # (the editor converts Serum tables as tools/wt_convert.py does)
node tests/host/wt_xcheck.mjs build/host/wtx
# (0.29) X0X's storage.c as built (tools/dn_patches.py): the settings objects in slot 3's top 16 KB
python3 tools/dn_patches.py storage x0x-firmware/firmware/src build/gen/storage_dn.c
# shellcheck disable=SC2086
$CC $W -Ix0x-firmware/firmware/src -Ix0x-firmware/firmware/src/app tests/host/store_test.c -o build/host/store_test
build/host/store_test
# shellcheck disable=SC2086
$CC $W tests/host/slices_xcheck.c src/dsp/dn_breaks.c -o build/host/slices_xcheck
build/host/slices_xcheck | node tests/host/slices_xcheck.mjs
# shellcheck disable=SC2086
$CC $W -Wno-unused-parameter -Wno-sign-compare tests/host/dn_host.c src/dsp/reese.c src/dsp/dn_seq.c src/dsp/dn_breaks.c src/dsp/dn_lfo.c src/dsp/dn_fx.c src/dsp/dn_fxc.c src/dsp/dn_fxq.c src/dsp/dn_fxm.c src/dsp/dn_fxl.c src/dsp/dn_sub.c src/dsp/dn_smp.c src/dsp/dn_fm.c src/dsp/dn_wt.c src/dsp/dn_ju.c -o build/host/dn_host
for s in tests/host/scenarios/*.txt; do
    build/host/dn_host "$s" build/host
done
# (0.46) the SMK's banded screen (src/core/dn_gfx.h, DN_GFX_BAND_ROWS): every scenario's screens, drawn band by band,
# the same pixels as drawn whole; and nothing left out of its lists
# shellcheck disable=SC2086
$CC $W -Wno-unused-parameter -Wno-sign-compare -DDN_GFX_BAND_ROWS=32 -DDN_GFX_OPS=640u -DDN_PBUF_SHARED="((uint8_t *)(void *)dg_strip)" -DDN_PAT_TMP_SHARED="((dn_pattern_t *)(void *)dg_ops)" -DDN_GFX_CHARS=2048u -DDN_GFX_ARGS=512u tests/host/dn_host.c src/dsp/reese.c src/dsp/dn_seq.c src/dsp/dn_breaks.c src/dsp/dn_lfo.c src/dsp/dn_fx.c src/dsp/dn_fxc.c src/dsp/dn_fxq.c src/dsp/dn_fxm.c src/dsp/dn_fxl.c src/dsp/dn_sub.c src/dsp/dn_smp.c src/dsp/dn_fm.c src/dsp/dn_wt.c src/dsp/dn_ju.c -o build/host/dn_host_band
mkdir -p build/host/band
for s in tests/host/scenarios/*.txt; do
    build/host/dn_host_band "$s" build/host/band > build/host/band/last.txt || { cat build/host/band/last.txt; echo "FAIL banded: $s"; exit 1; }
done
python3 tests/host/same_pngs.py build/host build/host/band
# (0.40) the second core: the same scenarios on one core make the same sound, to the bit
mkdir -p build/host/one
for s in fx juno fm wave sampler2 hitfx breaks lfo_wav; do
    DN_CORES=1 build/host/dn_host "tests/host/scenarios/$s.txt" build/host/one >/dev/null
done
for w in build/host/one/*.wav; do
    cmp -s "$w" "build/host/$(basename "$w")" || { echo "FAIL two cores vs one: $(basename "$w") differs"; exit 1; }
done
echo "ok   two cores vs one: $(ls build/host/one/*.wav | wc -l) recordings the same to the bit"
node tests/host/editor_golden.mjs build/host/editor-golden.json
# (0.44) the plugin's core (plugin/core): the app and the engines driven as a DAW drives them
# shellcheck disable=SC2086
$CC $W -Wno-unused-parameter -Wno-sign-compare -DDN_VERSION="\"$(tr -d ' \n' < VERSION)\"" plugin/core/dnc_test.c plugin/core/dn_core.c src/dsp/reese.c src/dsp/dn_seq.c src/dsp/dn_breaks.c src/dsp/dn_lfo.c src/dsp/dn_fx.c src/dsp/dn_fxc.c src/dsp/dn_fxq.c src/dsp/dn_fxm.c src/dsp/dn_fxl.c src/dsp/dn_sub.c src/dsp/dn_smp.c src/dsp/dn_fm.c src/dsp/dn_wt.c src/dsp/dn_ju.c -lm -o build/host/dnc_test
build/host/dnc_test build/host/plugin-core.ppm
echo "host tests: all ok"
