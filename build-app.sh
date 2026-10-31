#!/bin/bash
# ProJ IDE — native desktop app (GTK3 window + WebKitGTK view over the C++ engine).
# Needs: sudo apt install build-essential libwebkit2gtk-4.1-dev
set -e
cd "$(dirname "$0")"
mkdir -p build
tar --format=ustar -cf web.tar -C web .
ld -r -b binary web.tar -o build/web_tar.o
rm web.tar
ENGINE=$(ls src/*.cpp | grep -v server_main)
g++ -std=c++20 -O2 -Wall -Wextra -Isrc $ENGINE build/web_tar.o src_shell/shell_main.cpp -o projide-app \
    $(pkg-config --cflags --libs webkit2gtk-4.1) -pthread
echo "OK: ./projide-app"
