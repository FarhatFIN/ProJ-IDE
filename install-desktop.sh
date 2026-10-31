#!/bin/bash
# Registers ProJ IDE in the application menu (native window, no browser).
set -e
cd "$(dirname "$0")"
BIN="$(pwd)/projide-app"
mkdir -p ~/.local/share/applications
cat > ~/.local/share/applications/proj-ide.desktop <<DESK
[Desktop Entry]
Type=Application
Name=ProJ IDE
Comment=Компактный IDE: C++-движок, сборка, проекты
Exec=$BIN
Icon=application-x-executable
Terminal=false
Categories=Development;IDE;
StartupWMClass=ProJ IDE
DESK
echo "OK: ярлык «ProJ IDE» в меню приложений"
