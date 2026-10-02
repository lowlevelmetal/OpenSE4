#!/usr/bin/env bash
# Renders the scenes that compare builds and renderers pixel by pixel
# (docs/ENGINE.md, "Same on every platform"), and compares two such folders.
# Needs the player's installed game; the pictures show its art, so keep them
# out of the repository.
#
#   tools/render_scenes.sh render OUT RENDERER [PROGRAM...]
#       RENDERER: vulkan or opengl. PROGRAM defaults to build/debug/opense4.
#       The Windows build under Wine:
#         tools/render_scenes.sh render /tmp/win-vk vulkan \
#             wine build/dist-windows/opense4.exe --classic-dir="Z:/path/to/se4"
#       (run with SDL_VIDEO_DRIVER=offscreen for no window; WINEDLLOVERRIDES=
#       "winemenubuilder.exe=d" keeps Wine out of the desktop's menus).
#   tools/render_scenes.sh compare DIR_A DIR_B
#       For each picture: how many pixels differ and by how much at most
#       (ImageMagick).
#
# The runs use a scratch user data folder (OPENSE4_USER_DIR), so the player's
# own settings are neither read nor changed.

set -euo pipefail

render() {
    local out=$1 renderer=$2
    shift 2
    local program=("$@")
    [ ${#program[@]} -eq 0 ] && program=(build/debug/opense4)
    mkdir -p "$out"
    out=$(cd "$out" && pwd)
    local shot_prefix=""
    [ "${program[0]}" = wine ] && shot_prefix="Z:"
    local user
    user=$(mktemp -d)
    trap 'rm -rf "$user"' RETURN
    export OPENSE4_USER_DIR="$user"
    [ "${program[0]}" = wine ] && export OPENSE4_USER_DIR="Z:$user"
    printf '[options]\nshow_movement_lines = true\n' > "$user/classic_settings.toml"
    local common=(--seed=7 --no-audio "--renderer=$renderer" --size=1600x900)
    shot() {
        local name=$1
        shift
        "${program[@]}" "${common[@]}" "$@" "--screenshot=$shot_prefix$out/$name.png" > "$out/$name.log" 2>&1 || echo "failed: $name"
    }
    # A new game starts without ships (spec 01 §3.6): by turn 8 the first ones are under way.
    local game=(--quick-start=Terran --turns=8 --turn-style=simultaneous)
    shot main1024 "${game[@]}" --select=moving --open=none --layout=1024x768
    shot main800 "${game[@]}" --select=moving --open=none --layout=800x600
    shot main800-small "${game[@]}" --select=moving --open=none --layout=800x600 --size=1000x750
    shot planets --quick-start=Terran --turns=2 --open=planets --layout=1024x768
    shot research --quick-start=Terran --turns=2 --open=research --layout=1024x768
    shot design --quick-start=Terran --open=create-design --layout=1024x768
    shot tactical --quick-start=Terran --open=tactical-combat --layout=1024x768
    shot strategic --quick-start=Terran --open=strategic-combat --layout=1024x768 --frames=4
    shot manual --manual --layout=1024x768
    shot lesson --tutorial=exploring-and-colonizing:3 --open=none --layout=1024x768
}

compare() {
    local a=$1 b=$2
    for f in "$a"/*.png; do
        local name
        name=$(basename "$f")
        [ -f "$b/$name" ] || { printf '%-20s missing in %s\n' "$name" "$b"; continue; }
        magick "$f" "$b/$name" -alpha off -compose difference -composite -colorspace gray -depth 8 txt:- |
            awk -v n="$name" 'NR > 1 && $2 != "(0)" { g = $2; gsub(/[()]/, "", g); c++; if (g + 0 > m) m = g + 0 }
                               END { printf "%-20s %7d pixels differ, by at most %d/255\n", n, c, m }'
    done
}

case "${1:-}" in
    render) shift; render "$@" ;;
    compare) shift; compare "$@" ;;
    *) sed -n '2,20p' "$0"; exit 2 ;;
esac
