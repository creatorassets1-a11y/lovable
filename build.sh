#!/usr/bin/env bash
# One-command build for The Matron.
#
#   ./build.sh            build the release APK into dist/
#   ./build.sh --test     run the headless smoke test first
#   ./build.sh --assets   regenerate every asset (slow, ~40 min)
#
# Requires: JDK 17+, Gradle 8.9+, Android SDK 35, NDK 27, CMake 3.22+.

set -euo pipefail
cd "$(dirname "$0")"

ANDROID_HOME="${ANDROID_HOME:-/opt/android-sdk}"
export ANDROID_HOME
KEYSTORE="${MATRON_KEYSTORE:-android/keystore/matron.jks}"
VERSION="1.0"

for arg in "$@"; do
  case "$arg" in
    --assets)
      echo "==> levels";   python3 tools/gen_levels.py
      echo "==> textures"; python3 tools/gen_textures.py
      echo "==> meshes";   python3 tools/gen_meshes.py
      echo "==> font";     python3 tools/gen_font.py
      echo "==> sfx";      python3 tools/gen_sfx.py
      echo "==> voices";   python3 tools/gen_voice.py
      echo "==> icons";    python3 tools/gen_icons.py
      # The mixer expects everything at its own rate; normalise here rather
      # than resampling on a phone at load time.
      for f in android/app/src/main/assets/audio/*/*.ogg; do
        ffmpeg -y -loglevel error -i "$f" -ar 22050 -c:a libvorbis -qscale:a 3 "$f.tmp.ogg"
        mv "$f.tmp.ogg" "$f"
      done
      ;;
    --test)
      echo "==> host build"
      cmake -S host -B build/host -DCMAKE_BUILD_TYPE=Release >/dev/null
      cmake --build build/host -j"$(nproc)"
      echo "==> headless smoke test"
      ./build/host/matron_game android/app/src/main/assets dist/shots3d --frames 5400
      ;;
  esac
done

if [ ! -f "$KEYSTORE" ]; then
  echo "==> generating local signing key at $KEYSTORE"
  mkdir -p "$(dirname "$KEYSTORE")"
  keytool -genkeypair -v -keystore "$KEYSTORE" -storetype JKS \
    -keyalg RSA -keysize 2048 -validity 10950 \
    -alias "${MATRON_KEY_ALIAS:-matron}" \
    -storepass "${MATRON_STORE_PASS:-matronmatron}" \
    -keypass "${MATRON_KEY_PASS:-matronmatron}" \
    -dname "CN=The Matron, OU=Games, O=Blackmoor, L=Unknown, S=Unknown, C=US" \
    >/dev/null 2>&1
fi

echo "sdk.dir=$ANDROID_HOME" > android/local.properties

echo "==> assembling release APK"
( cd android && gradle --no-daemon assembleRelease )

mkdir -p dist
cp android/app/build/outputs/apk/release/app-release.apk "dist/TheMatron-$VERSION.apk"
"$ANDROID_HOME"/build-tools/*/apksigner verify "dist/TheMatron-$VERSION.apk"

echo
echo "built dist/TheMatron-$VERSION.apk ($(du -h "dist/TheMatron-$VERSION.apk" | cut -f1))"
echo "install with: adb install -r dist/TheMatron-$VERSION.apk"
