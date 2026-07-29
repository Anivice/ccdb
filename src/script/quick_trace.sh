#!/usr/bin/env bash

log="$1"
# shellcheck disable=SC2046
trace.sh ./ccdb objdump $(grep --text landmark < "$log" | sed -E 's/.*(0x[0-9|A-Z]+).*/\1/g') addr2line < "$log"
