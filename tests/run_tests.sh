#!/bin/sh
# Builds and runs the platform-independent test suites on the host machine.
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT="$DIR/.."
CPP="$ROOT/app/src/main/cpp"
OUT=${OUT:-/tmp/hollow-build}
mkdir -p "$OUT"

# pak_verify is C, and is compiled as C so the test exercises exactly the
# translation unit the app ships.
clang -std=c11 -O2 -Wall -c "$CPP/pak_verify.c" -o "$OUT/pak_verify.o"

clang++ -std=c++17 -O2 -DHOLLOW_NO_OPENSL -Wall \
    "$DIR/host_test.cpp" \
    "$CPP/world.cpp" "$CPP/actors.cpp" "$CPP/missions.cpp" \
    "$CPP/player.cpp" "$CPP/audio.cpp" "$CPP/assets.cpp" "$CPP/speech.cpp" \
    "$OUT/pak_verify.o" \
    -o "$OUT/hollow_tests"

clang++ -std=c++17 -O2 -Wall \
    "$DIR/voice_test.cpp" "$CPP/speech.cpp" \
    -o "$OUT/hollow_voice_tests"

"$OUT/hollow_voice_tests"
echo
exec "$OUT/hollow_tests" "$ROOT/app/src/main/assets/hollow.pak"
