#!/usr/bin/env bash
set -euo pipefail
random_name()
{
    seed=$((RANDOM % 10 + 1));
    name=$(dd if=/dev/random bs=32 count=4 2>/dev/null | sha512sum | base64 | cut -d $'\n' -f 2 | cut -b $seed-$((seed + 64)))
    echo "$name" | head -c 16 | tail -c 8;
}

json_target="$1"
dest="$2"
NAME="$(random_name)"
tmp="/tmp/${NAME}.d"
mkdir -p "$tmp"

{
    files=$(ls -la "${CMAKE_SOURCE_DIR}"/additional_help/additional_help.*.txt | wc -l)
    i=1
    echo "  {"
    for file in "${CMAKE_SOURCE_DIR}"/additional_help/additional_help.*.txt;
    do
        lang="$(echo "$file" | sed -E 's/.*additional_help\.([a-z|A-Z|_|-]+)\.txt$/\1/g')"
        printf '    "%s": "%s"' "$lang" "$(sed ':a;N;$!ba;s/\n/\\n/g' < "$file" | sed 's/\"/\\\"/g')"
        if [[ $i -lt $files ]]; then
            echo ","
        else
            echo ""
        fi
        ((i++))
    done
    echo "  }"
} > "$tmp/json_tailing"

tr ']' ',' > "$tmp/json_head" < "$json_target"
cat "$tmp/json_tailing" >> "$tmp/json_head"
echo "]" >> "$tmp/json_head"
cp "$tmp/json_head" "$dest"
rm -rf "$tmp"
