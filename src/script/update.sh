#!/usr/bin/env bash
set -euo pipefail
DEST="$1"

if [ -z "$DEST" ]; then
    echo "update.sh [Destination]"
    exit 1
fi

ARCH="$(uname -m | sed 's/armv7l/armv7hf/g')"
VER="$(git ls-remote https://github.com/Anivice/ccdb/ HEAD | head -c 8)"
wget https://github.com/Anivice/ccdb/releases/download/ccdb.NightlyBuild."$VER"/ccdb."$ARCH" -O ccdb
