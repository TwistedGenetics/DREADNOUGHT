#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# CI: run a step's command; if it fails, its last lines become the job's annotation (readable without the logs).
#   tools/ci_step.sh TITLE COMMAND [ARGS ...]
title=$1
shift
log=$(mktemp)
set -o pipefail
if "$@" 2>&1 | tee "$log"; then
    rm -f "$log"
    exit 0
fi
rc=$?
msg=$( (grep -E "error|Error|FAIL|fatal" "$log" | head -20; echo ---; tail -30 "$log") | cut -c1-300)
msg=${msg//'%'/'%25'}
msg=${msg//$'\r'/}
msg=${msg//$'\n'/'%0A'}
echo "::error title=$title::$msg"
rm -f "$log"
exit $rc
