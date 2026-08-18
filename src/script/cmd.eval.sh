#!/usr/bin/env bash
set -euo pipefail
CMDSCRIPT="$(realpath $1)"
relDir="$(dirname "$CMDSCRIPT")"
tmp=$(mktemp)
cp "$CMDSCRIPT" $tmp

grep -Eo '\%(.*)\%' < "$CMDSCRIPT" | while read -r evl;
do
    value="$(realpath "$relDir/$(sed 's/\%//g' <<< $evl)")"
    sed -i 's#'"$(sed 's/\%/\\\%/g' <<< $evl)"'#'"CCDBEVALENABLED_Base32_$(cat "$value" | base32 -w0 | sed 's/\=/\\\=/g')"'#g' $tmp
done

cat $tmp
rm $tmp