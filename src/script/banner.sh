#!/usr/bin/env bash

banner_lines=3
FRAME_FILE="/tmp/.frame_index"
FRAME_COUNT=8

if [[ -f "$FRAME_FILE" ]]; then
    read -r frame < "$FRAME_FILE"
else
    frame=0
fi

if [[ ! "$frame" =~ ^[0-9]+$ ]]; then
    frame=0
fi

frame=$((frame % FRAME_COUNT))
banner=$({
case "$frame" in
    0)
        cat <<'EOF'
        o
       /|\
       / \
EOF
        ;;
    1)
        cat <<'EOF'
         o
        /|\
        / \
EOF
        ;;
    2)
        cat <<'EOF'
          o
         /|\
         / \
EOF
        ;;
    3)
        cat <<'EOF'
           o
          /|\
          / \
EOF
        ;;
    4)
        cat <<'EOF'
          o
         /|\
         / \
EOF
        ;;
    5)
        cat <<'EOF'
         o
        /|\
        / \
EOF
        ;;
    6)
        cat <<'EOF'
        \o/
         |
        / \
EOF
        ;;
    7)
        cat <<'EOF'
         o_
        /|
        / \
EOF
        ;;
esac
})

next=$(((frame + 1) % FRAME_COUNT))
printf '%d\n' "$next" > "$FRAME_FILE"

jq -n \
  --argjson banner_lines "$banner_lines" \
  --arg     banner       "$banner" \
  '{banner_lines: $banner_lines, banner: $banner}'