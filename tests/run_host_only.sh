#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# DREADNOUGHT: the host tests that need no JieLi toolchain (generated headers only), for quick local
# iteration on a Mac or Linux. The full suite (tests/run_tests.sh) runs in CI after the target build.
#   tests/run_host_only.sh [name-filter]
set -e
cd "$(dirname "$0")/.."
python3 -c "import sys; sys.path.insert(0, 'tools'); import build; build.generate()" >/dev/null
OUT=build/host
mkdir -p "$OUT"
CC="${CC:-cc}"
F="${1:-}"
fail=0
t() {  # name, source, extra flags...
    name=$1; src=$2; shift 2
    case "$name" in *"$F"*) ;; *) return 0;; esac
    if $CC -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal "$@" -o "$OUT/$name" "tests/$src" -lm; then
        if "$OUT/$name" $ARGS >"$OUT/$name.log" 2>&1; then echo "ok   $name"; else echo "FAIL $name (see $OUT/$name.log)"; fail=1; fi
    else
        echo "FAIL $name (compile)"; fail=1
    fi
}
ARGS="" t storage storage_test.c
ARGS="" t recovery recovery_test.c
ARGS="" t seq2 seq2_test.c
ARGS="$OUT/drum-kits.wav $OUT/drum-kits.txt" t drumkit drumkit_test.c
ARGS="$OUT/punch-fx.wav" t punch punch_test.c
ARGS="" t fx251 fx251_test.c
ARGS="$OUT" t ui_pages ui_pages_test.c
ARGS="" t project project_test.c
ARGS="0 0 1 $OUT/render.wav" t hostsim hostsim.c
ARGS="tests/golden.txt tests/cpu_baseline.txt" SKIP_CPU_VALGRIND=1 t regress regress.c
ARGS="1" t soak soak_test.c
[ $fail -eq 0 ] && echo "HOST-ONLY TESTS PASSED" || { echo "HOST-ONLY TESTS FAILED"; exit 1; }
