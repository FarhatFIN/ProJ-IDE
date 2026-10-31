#!/bin/bash
# ProJ IDE — desktop FLTK variant (v2 UI). Needs libfltk1.3-dev.
set -e
cd "$(dirname "$0")"
g++ -std=c++20 -O2 -Wall -Wextra src/*.cpp src_fltk/*.cpp -o projide-fltk \
    $(fltk-config --cxxflags --ldflags --libs) -pthread
echo "OK: ./projide-fltk"
