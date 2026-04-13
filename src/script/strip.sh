#!/usr/bin/env bash

if [ -n "$STRIP" ]; then
    echo "Stripping $1"
    "$(realpath "$STRIP")" "$1"
fi
