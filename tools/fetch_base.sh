#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# DREADNOUGHT Phase 0: fetch the pinned, unmodified base firmware we build in CI to prove the
# build -> package -> flash loop before any of our own code goes in.
#   tools/fetch_base.sh [DEST]        default: build/base
# The base is SLOOP (isod89/sloop-fm1, GPL-3.0-only, after Felucca by Leo Kuroshita / Hügelton Instruments).
# Changing the pin is a deliberate commit: update BASE_COMMIT and BASE_TAG together.
set -eu
BASE_REPO="https://github.com/isod89/sloop-fm1.git"
BASE_TAG="v2.8.1"
BASE_COMMIT="d89364253b79a5c6002c0d90c5f16b3c3c16ea9d"
DEST="${1:-build/base}"

if [ -d "$DEST/.git" ] && [ "$(git -C "$DEST" rev-parse HEAD)" = "$BASE_COMMIT" ]; then
    echo "fetch_base: $DEST already at $BASE_TAG"
    exit 0
fi
rm -rf "$DEST"
git clone --depth 1 --branch "$BASE_TAG" --recurse-submodules --shallow-submodules "$BASE_REPO" "$DEST"
GOT="$(git -C "$DEST" rev-parse HEAD)"
[ "$GOT" = "$BASE_COMMIT" ] || { echo "fetch_base: $BASE_TAG is $GOT, expected $BASE_COMMIT (tag moved?)"; exit 1; }
echo "fetch_base: $BASE_TAG ($BASE_COMMIT) in $DEST"
