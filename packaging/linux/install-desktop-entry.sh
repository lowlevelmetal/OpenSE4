#!/bin/sh
# Adds OpenSE4 to the desktop's application list (GNOME, KDE Plasma and other
# freedesktop.org desktops) for the current user. The entry runs the game from
# the folder this script is in: run the script again after moving the folder.
#
#   ./install-desktop-entry.sh              add the entry and the icons
#   ./install-desktop-entry.sh --uninstall  remove them again

set -eu

id=io.github.lowlevelmetal.OpenSE4
here=$(cd "$(dirname "$0")" && pwd -P)
data=${XDG_DATA_HOME:-$HOME/.local/share}
apps="$data/applications"
icons="$data/icons/hicolor"

refresh() {
    if command -v update-desktop-database > /dev/null 2>&1; then
        update-desktop-database -q "$apps" 2> /dev/null || true
    fi
    if [ -f "$icons/icon-theme.cache" ] && command -v gtk-update-icon-cache > /dev/null 2>&1; then
        gtk-update-icon-cache -q -f -t "$icons" 2> /dev/null || true
    fi
}

case "${1:-}" in
    "") ;;
    --uninstall)
        rm -f "$apps/$id.desktop"
        for icon in "$icons"/*/apps/"$id".png "$icons"/*/apps/"$id".svg; do
            [ -e "$icon" ] && rm -f "$icon"
        done
        refresh
        echo "Removed OpenSE4 from the application list."
        exit 0 ;;
    *)
        echo "usage: $0 [--uninstall]" >&2
        exit 2 ;;
esac

game="$here/opense4"
template="$here/share/applications/$id.desktop"
if [ ! -x "$game" ] || [ ! -f "$template" ]; then
    echo "Run this script from the unpacked OpenSE4 folder (opense4 and share/ are missing)." >&2
    exit 1
fi
# GLib (GNOME) checks that the program exists before undoing the "%%" escape a
# percent sign needs, so such an entry would never show up there.
case "$game" in
    *"
"* | *%*) echo "The folder's path contains a line break or a percent sign, which desktops cannot start programs from. Move OpenSE4 to another folder." >&2; exit 1 ;;
esac

for dir in "$here"/share/icons/hicolor/*/apps; do
    size=$(basename "$(dirname "$dir")")
    mkdir -p "$icons/$size/apps"
    cp "$dir/$id".* "$icons/$size/apps/"
done
touch "$icons"

# Exec takes the program quoted, escaped as the Desktop Entry Specification
# requires; TryExec takes the plain path, which hides the entry once the folder
# is gone.
exec_path=$(printf '%s' "$game" | sed -e 's/\\/\\\\\\\\/g' -e 's/"/\\\\"/g' -e 's/`/\\\\`/g' -e 's/\$/\\\\$/g')
try_path=$(printf '%s' "$game" | sed -e 's/\\/\\\\/g')
mkdir -p "$apps"
tmp="$apps/.$id.desktop.tmp"
while IFS= read -r line; do
    case "$line" in
        Exec=*) printf 'Exec="%s"\n' "$exec_path" ;;
        TryExec=*) printf 'TryExec=%s\n' "$try_path" ;;
        *) printf '%s\n' "$line" ;;
    esac
done < "$template" > "$tmp"
mv "$tmp" "$apps/$id.desktop"
refresh

echo "Added OpenSE4 to the application list ($apps/$id.desktop)."
echo "It runs $game."
