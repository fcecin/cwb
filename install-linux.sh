#!/usr/bin/env bash
# Linux desktop integration for cwb: install the Araucaria icon into the user's
# hicolor theme and a cwb.desktop entry, so the GNOME/Ubuntu dock shows the icon
# (Wayland ignores the runtime window icon and matches the window's app_id to a
# .desktop instead). Per-user; no root. Re-run after moving the binary.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"

bin="$here/build/cwb"
[ -x "$bin" ] || { echo "build cwb first: ./build.sh"; exit 1; }

icons="$HOME/.local/share/icons/hicolor"
apps="$HOME/.local/share/applications"
mkdir -p "$apps"

# Render the icon at each theme size straight from the embedded SVG (Qt's
# renderer -- ImageMagick without librsvg botches the gradients).
for s in 16 32 48 64 128 256; do
  d="$icons/${s}x${s}/apps"; mkdir -p "$d"
  QT_QPA_PLATFORM=offscreen "$bin" iconshot "$d/cwb.png" --size "$s" >/dev/null 2>&1
done
mkdir -p "$icons/scalable/apps"
cp -f "$here/resources/cwb.svg" "$icons/scalable/apps/cwb.svg"

# Desktop entry, Exec resolved to this build.
sed "s|^Exec=.*|Exec=$bin %u|" "$here/resources/cwb.desktop" > "$apps/cwb.desktop"
chmod +x "$apps/cwb.desktop"

# A per-user hicolor index + cache, so the lookup and cache find the icon.
[ -f "$icons/index.theme" ] || cp -f /usr/share/icons/hicolor/index.theme \
  "$icons/index.theme" 2>/dev/null || true
gtk-update-icon-cache -f "$icons" >/dev/null 2>&1 || true
update-desktop-database "$apps" >/dev/null 2>&1 || true

echo "installed cwb.desktop + icons. If the dock still shows a gear, log out/in"
echo "or restart the shell so GNOME re-reads the desktop database."
