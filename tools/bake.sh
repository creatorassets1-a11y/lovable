#!/bin/sh
# Builds and runs the offline asset bake. Must be run before assembling the
# APK; the output is app/src/main/assets/hollow.pak.
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT="$DIR/.."
mkdir -p "$ROOT/app/src/main/assets"
clang++ -std=c++17 -O2 -Wall \
    "$DIR/bake.cpp" "$DIR/materials.cpp" \
    "$ROOT/app/src/main/cpp/speech.cpp" \
    -o /tmp/hollow_bake
exec /tmp/hollow_bake "$ROOT/app/src/main/assets/hollow.pak"
