#!/bin/bash
# ProJ IDE — server variant: static Linux binary, web/ embedded inside.
# Needs: g++ >= 10, binutils (ld), tar. No runtime dependencies.
set -e
cd "$(dirname "$0")"
mkdir -p build
tar --format=ustar -cf web.tar -C web .
ld -r -b binary web.tar -o build/web_tar.o
rm web.tar
g++ -std=c++20 -O2 -Wall -Wextra -Isrc src/*.cpp build/web_tar.o -o projide \
    -static -static-libgcc -static-libstdc++ -pthread
echo "OK: ./projide (статический, веб-интерфейс вшит)"
