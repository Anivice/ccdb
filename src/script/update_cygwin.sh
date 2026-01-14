#!/usr/bin/env bash
set -euo pipefail
DEST="$1"

if [ -z "$DEST" ]; then
    echo "update.sh [Destination]"
    exit 1
fi

VER="$(git ls-remote https://github.com/Anivice/ccdb/ HEAD | head -c 8)"
wget https://github.com/Anivice/ccdb/releases/download/ccdb.cygwin.NightlyBuild."$VER"/ccdb.exe -O "$DEST"
