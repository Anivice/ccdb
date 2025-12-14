#!/usr/bin/env bash
set -euo pipefail
script_dir="$(dirname "$(readlink -f "$0")")"

ARCH="$1"
BUILD_DIR="$2"
TARGET="$(basename "$script_dir/../toolchains"/"$ARCH"-*/bin/*-addr2line | awk -F'-' '{ for (i=1; i<NF; i++) printf "%s%s", $i, (i<NF-1?OFS:RS) }' | tr ' ' '-')"
TARGET=$(echo $TARGET)
if [ -z "$TARGET" ]; then echo "Unknown arch $ARCH" >&2; exit 1; fi

CMAKE_CFLAGS="-O3 -fomit-frame-pointer -ffast-math -fstrict-aliasing -fdata-sections -ffunction-sections -D_FORTIFY_SOURCE=2 -fno-stack-protector -Wl,-z,relro -Wl,-z,now -s"
export CXXFLAGS="$CMAKE_CFLAGS"
export CFLAGS="$CMAKE_CFLAGS"
export CC="$TARGET"-gcc
export CXX="$TARGET"-g++
export AR="$TARGET"-ar
export RANLIB="$TARGET"-ranlib
export STRIP="$TARGET"-strip
MUSL_SYSROOT="$(echo "$script_dir/../toolchains/$ARCH-"*)"
export MUSL_SYSROOT="$MUSL_SYSROOT/"
echo "$MUSL_SYSROOT"
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
            -DNCURSES_MAKE_ADDITIONAL_FLAGS="CFLAGS=\"$CMAKE_CFLAGS\" CXXFLAGS=\"$CMAKE_CFLAGS\" -j$(nproc)" \
            -DREADLINE_MAKE_ADDITIONAL_FLAGS="CFLAGS=\"$CMAKE_CFLAGS\" CXXFLAGS=\"$CMAKE_CFLAGS\" -j$(nproc)" \
            -DCMAKE_BUILD_STATIC="True"
pushd "$PWD"
cd "$BUILD_DIR"
env PATH="$MUSL_SYSROOT"/bin/:"$PATH" make CFLAGS="$CMAKE_CFLAGS" CXXFLAGS="$CMAKE_CFLAGS" -j"$(nproc)"
popd
