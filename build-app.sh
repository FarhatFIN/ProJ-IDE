#!/bin/bash
# ProJ IDE — native desktop app (FLTK, no web layer). 
# Needs: sudo apt install build-essential libfltk1.3-dev
set -e
cd "$(dirname "$0")"
g++ -std=c++20 -O2 -Wall -Wextra -Isrc src/*.cpp src_fltk/*.cpp -o projide-app \
    $(fltk-config --cxxflags --ldflags --libs) -pthread
echo "OK: ./projide-app"
