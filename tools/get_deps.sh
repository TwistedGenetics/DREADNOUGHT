#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# DREADNOUGHT: fetch the build dependencies (Linux x86-64; on a Mac use the GitHub Actions build).
#   tools/get_deps.sh [DEST]          default: ~/.dreadnought-deps
# Installs:
#   DEST/toolchain  JieLi Linux toolchain (clang 4.0.1 for pi32v2), from JieLi's package server
#   DEST/ac79       the three Apache-2.0 files of the JieLi AC79 SDK the packager needs,
#                   in the SDK's own layout (cpu/wl82/tools/...), checked against pinned SHA-256
# Prints JIELI_TOOLCHAIN=... and AC79_SDK=... lines (GitHub Actions: appended to $GITHUB_ENV).
set -eu
DEST="${1:-$HOME/.dreadnought-deps}"
mkdir -p "$DEST"
DEST="$(cd "$DEST" && pwd)"

# --- toolchain ---------------------------------------------------------------------------------
if [ ! -x "$DEST/toolchain/pi32v2/bin/clang" ]; then
    echo "get_deps: downloading the JieLi Linux toolchain"
    curl -fL --retry 3 https://pkgman.jieliapp.com/s/linux-toolchain | tar -xJ -C "$DEST"
    TC="$(ls -d "$DEST"/jieli-linux-toolchains-* | sort | tail -1)"
    ln -sfn "$(basename "$TC")" "$DEST/toolchain"
fi
[ -x "$DEST/toolchain/pi32v2/bin/clang" ] || { echo "get_deps: no pi32v2 clang in $DEST/toolchain"; exit 1; }

# --- AC79 SDK files (the version SLOOP / Felucca test with) ------------------------------------
SDK_REPO="https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git"
SDK_TAG="AC79NN_SDK_V1.2.1_2023-12-13"
SDK="$DEST/ac79"
T="cpu/wl82/tools"
check() {  # file sha256
    [ -f "$SDK/$T/$1" ] && echo "$2  $SDK/$T/$1" | sha256sum -c --status
}
have_sdk() {
    check uboot.boot        4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4 &&
    check cfg_tool.bin      276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b &&
    check cfg/eq_cfg_hw.bin 41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d
}
if ! have_sdk; then
    echo "get_deps: fetching 3 files of the JieLi AC79 SDK ($SDK_TAG)"
    rm -rf "$SDK" "$DEST/sdk-clone"
    # sparse, blob-less: only the three files are downloaded, not the whole SDK
    if git clone --depth 1 --branch "$SDK_TAG" --filter=blob:none --sparse "$SDK_REPO" "$DEST/sdk-clone"; then
        git -C "$DEST/sdk-clone" sparse-checkout set --no-cone \
            "/$T/uboot.boot" "/$T/cfg_tool.bin" "/$T/cfg/eq_cfg_hw.bin"
    else
        echo "get_deps: sparse clone refused, cloning the tag (large)"
        git clone --depth 1 --branch "$SDK_TAG" "$SDK_REPO" "$DEST/sdk-clone"
    fi
    mkdir -p "$SDK/$T/cfg"
    cp "$DEST/sdk-clone/$T/uboot.boot" "$DEST/sdk-clone/$T/cfg_tool.bin" "$SDK/$T/"
    cp "$DEST/sdk-clone/$T/cfg/eq_cfg_hw.bin" "$SDK/$T/cfg/"
    cp "$DEST/sdk-clone/LICENSE" "$SDK/LICENSE" 2>/dev/null || true
    rm -rf "$DEST/sdk-clone"
    have_sdk || { echo "get_deps: AC79 SDK files do not match the pinned SHA-256"; exit 1; }
fi

echo "JIELI_TOOLCHAIN=$DEST/toolchain"
echo "AC79_SDK=$SDK"
if [ -n "${GITHUB_ENV:-}" ]; then
    echo "JIELI_TOOLCHAIN=$DEST/toolchain" >> "$GITHUB_ENV"
    echo "AC79_SDK=$SDK" >> "$GITHUB_ENV"
fi
