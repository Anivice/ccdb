#!/usr/bin/env bash
set -euo pipefail

LC_ALL=C

BEGIN="0.0"
END="100.0"

die() {
    echo "error: $*" >&2
    exit 1
}

command -v jq >/dev/null 2>&1 || die "jq is required"
command -v bc >/dev/null 2>&1 || die "bc is required"

json_number() {
    local value="$1"

    case "$value" in
        .*)
            printf '0%s' "$value"
            ;;
        -.*)
            printf -- '-0%s' "${value#-}"
            ;;
        *)
            printf '%s' "$value"
            ;;
    esac
}

clamp_rgb() {
    local value="$1"

    if [[ "$(bc_eval "$value < 0")" == "1" ]]; then
        printf '0'
    elif [[ "$(bc_eval "$value > 255")" == "1" ]]; then
        printf '255'
    else
        printf '%s' "$value"
    fi
}

parse_json()
{
    printf '%s' "$1" | jq -er '
        if type != "object" then
            error("input must be a JSON object")
        elif (.'"$2"' | type) != "number" then
            error("offset must be a JSON number")
        else
            .'"$2"'
        end
    '
}

offset="$(parse_json "$1" "offset")" || die 'expected JSON'
SCALE="$(parse_json "$1" "precision")" || die 'expected JSON'
offset=$(printf "%.${SCALE}f" "$offset")

bc_eval() {
    printf 'scale=%s; %s\n' "$SCALE" "$1" | BC_LINE_LENGTH=0 bc -l | sed -e :1 -e '/\\$/{N;s/\\\n//;b1' -e '}'
}

[[ "$(bc_eval "$END <= $BEGIN")" != "1" ]] ||
    die "END must be greater than BEGIN"

is_sentinel="$(bc_eval "$offset == -1")"
is_before_begin="$(bc_eval "$offset < $BEGIN")"
is_after_end="$(bc_eval "$offset > $END")"

if [[ "$is_sentinel" == "1" ||
      "$is_before_begin" == "1" ||
      "$is_after_end" == "1" ]]; then

    printf '{"Begin": %s, "End": %s, "R": 0, "G": 0, "B": 0}\n' \
        "$(json_number "$BEGIN")" \
        "$(json_number "$END")"
    echo "Faulty: $is_sentinel, $is_before_begin, $is_after_end, $1" >&2
    exit 0
fi

# Normalize offset into a rainbow position:
# BEGIN -> 0
# END   -> 1530
#
# 1530 = 6 color segments × 255:
# red -> yellow -> green -> cyan -> blue -> magenta -> red
position="$(bc_eval "(($offset - $BEGIN) * 1530) / ($END - $BEGIN)")"

if [[ "$(bc_eval "$position <= 255")" == "1" ]]; then
    # Red -> Yellow
    r="255"
    g="$position"
    b="0"

elif [[ "$(bc_eval "$position <= 510")" == "1" ]]; then
    # Yellow -> Green
    r="$(bc_eval "510 - $position")"
    g="255"
    b="0"

elif [[ "$(bc_eval "$position <= 765")" == "1" ]]; then
    # Green -> Cyan
    r="0"
    g="255"
    b="$(bc_eval "$position - 510")"

elif [[ "$(bc_eval "$position <= 1020")" == "1" ]]; then
    # Cyan -> Blue
    r="0"
    g="$(bc_eval "1020 - $position")"
    b="255"

elif [[ "$(bc_eval "$position <= 1275")" == "1" ]]; then
    # Blue -> Magenta
    r="$(bc_eval "$position - 1020")"
    g="0"
    b="255"

else
    # Magenta -> Red
    r="255"
    g="0"
    b="$(bc_eval "1530 - $position")"
fi

r="$(clamp_rgb "$r")"
g="$(clamp_rgb "$g")"
b="$(clamp_rgb "$b")"

printf '{"Begin": %s, "End": %s, "R": %s, "G": %s, "B": %s}\n' \
    "$(json_number "$BEGIN")" \
    "$(json_number "$END")" \
    "$(json_number "$r")" \
    "$(json_number "$g")" \
    "$(json_number "$b")"