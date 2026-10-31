#!/bin/bash
# ProJ IDE — Windows cross-build (server + browser mode, self-contained exe).
# Needs: sudo apt install g++-mingw-w64-x86-64
set -e
cd "$(dirname "$0")"
mkdir -p build
tar --format=ustar -cf web.tar -C web .
x86_64-w64-mingw32-ld -r -b binary web.tar -o build/web_tar_mingw.o
rm web.tar
x86_64-w64-mingw32-g++ -std=c++20 -O2 -Wall -Wextra -Isrc src/*.cpp build/web_tar_mingw.o \
    -o "ProJ-IDE.exe" \
    -static -static-libgcc -static-libstdc++ -pthread -lws2_32 -lshell32
echo "OK: ProJ-IDE.exe (self-contained)"
