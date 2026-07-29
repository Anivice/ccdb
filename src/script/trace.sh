#!/usr/bin/env bash

set -euo pipefail
readonly CCDB="$1"
readonly OBJDUMP="$2"
readonly LANDMARK="$3"
readonly ADDR2LINE="$4"
readonly SIGNARURE="================================== TRACER =================================="

landmark_sym="0x$("$OBJDUMP" -Tt "$CCDB" | grep landmark | awk '{print $1}')"
offset="$(printf "%d - %d\n" "$LANDMARK" "$landmark_sym" | bc)"

function ccdb_addr2line()
{
    addr="$1"
    ori="$(printf "%d - %d\n" "$addr" "$offset" | bc)"
    "$ADDR2LINE" --demangle -f -p -a -e "$CCDB" "$(printf "%X" "$ori")"
}

start=0
while read -r line;
do
    if [[ "X$line" == "X$SIGNARURE" ]]; then ((start += 1)); continue; fi
    if [ $start -eq 1 ]; then ccdb_addr2line "$line"; fi
    if [ $start -eq 2 ]; then break; fi
done
