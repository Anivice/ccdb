#!/usr/bin/env bash
set -euo pipefail
script_dir="$(dirname "$(readlink -f "$0")")"

ARCH="$1"
BUILD_DIR="$2"
TARGET="$(basename "$script_dir/../toolchains"/"$ARCH"-*/bin/*-addr2line | awk -F'-' '{ for (i=1; i<NF; i++) printf "%s%s", $i, (i<NF-1?OFS:RS) }' | tr ' ' '-')"

if [ -z "$TARGET" ]; then echo "Unknown arch $ARCH" >&2; exit 1; fi

export CC="$TARGET"-gcc
export CXX="$TARGET"-g++
export AR="$TARGET"-ar
export RANLIB="$TARGET"-ranlib
export STRIP="$TARGET"-strip
export MUSL_SYSROOT="$script_dir/../toolchains/$ARCH-linux-musl-cross/"

env PATH="$MUSL_SYSROOT"/bin/:"$PATH" cmake -B "$BUILD_DIR" -S "$script_dir" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_SYSTEM_NAME=Linux \
            -DCMAKE_C_COMPILER="$CC" \
            -DCMAKE_CXX_COMPILER="$CXX" \
            -DCMAKE_FIND_ROOT_PATH="$MUSL_SYSROOT" \
            -DCMAKE_EXE_LINKER_FLAGS="-static -s" \
            -DCC_ADDITIONAL_OPTIONS=-static \
            -DLD_ADDITIONAL_OPTIONS=-static \
            -DREADLINE_CONFIGURE_ADDITIONAL_FLAGS="--host=$ARCH" \
            -DNCURSES_CONFIGURE_ADDITIONAL_FLAGS="--disable-stripping;--host=$ARCH" \
            -DCMAKE_STRIP="$STRIP" \
            -DNCURSES_MAKE_ADDITIONAL_FLAGS="-j$(nproc)" \
            -DREADLINE_MAKE_ADDITIONAL_FLAGS="-j$(nproc)"

env PATH="$MUSL_SYSROOT"/bin/:"$PATH" cmake --build "$BUILD_DIR"
