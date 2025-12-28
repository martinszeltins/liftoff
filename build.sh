#!/bin/bash
# Build and run script for Liftoff

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DESKTOP_FILE="$HOME/.local/share/applications/lv.martinsz.Liftoff.desktop"
ICON_PATH="$SCRIPT_DIR/data/icons/hicolor/scalable/apps/lv.martinsz.Liftoff.png"

echo "==> Setting up build directory..."
meson setup build --wipe

echo "==> Compiling..."
meson compile -C build

# Install .desktop file if it doesn't exist
if [ ! -f "$DESKTOP_FILE" ]; then
    echo "==> Installing .desktop entry..."
    mkdir -p "$HOME/.local/share/applications"
    cat > "$DESKTOP_FILE" << EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=Liftoff
Comment=Create Desktop Launchers
Exec=$SCRIPT_DIR/build/src/liftoff %F
Icon=$ICON_PATH
Categories=Utility;Development;
Terminal=false
EOF
    chmod +x "$DESKTOP_FILE"
    echo "    Desktop entry installed at $DESKTOP_FILE"
fi

echo "==> Running Liftoff..."
./build/src/liftoff "$@"
