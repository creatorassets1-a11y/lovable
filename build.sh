#!/usr/bin/env bash
# One-command build for The Ninth Loop.
#
#   ./build.sh            build the release APK into dist/
#   ./build.sh --assets   regenerate audio + icons first (slow, needs espeak-ng)
#   ./build.sh --test     run the headless game tests before building
#
# Requires: JDK 17+, Gradle 8.9+, Android SDK (platform 35, build-tools 35).
# Point ANDROID_HOME at your SDK, or leave android/local.properties in place.

set -euo pipefail
cd "$(dirname "$0")"

ANDROID_HOME="${ANDROID_HOME:-/opt/android-sdk}"
export ANDROID_HOME

KEYSTORE="${NINTHLOOP_KEYSTORE:-android/keystore/ninthloop.jks}"
VERSION="1.0"

for arg in "$@"; do
  case "$arg" in
    --assets)
      echo "==> regenerating icons"
      python3 tools/gen_icons.py
      echo "==> synthesising voice lines (this takes a few minutes)"
      python3 tools/gen_voice.py
      echo "==> synthesising sound effects"
      python3 tools/gen_sfx.py
      ;;
    --test)
      echo "==> headless game tests"
      node tools/test_game.js
      ;;
  esac
done

# Release signing. A throwaway key is generated on first build so the repo does
# not have to carry one; set NINTHLOOP_KEYSTORE and friends to sign for real.
if [ ! -f "$KEYSTORE" ]; then
  echo "==> generating local signing key at $KEYSTORE"
  mkdir -p "$(dirname "$KEYSTORE")"
  keytool -genkeypair -v -keystore "$KEYSTORE" -storetype JKS \
    -keyalg RSA -keysize 2048 -validity 10950 \
    -alias "${NINTHLOOP_KEY_ALIAS:-ninthloop}" \
    -storepass "${NINTHLOOP_STORE_PASS:-ninthloop}" \
    -keypass "${NINTHLOOP_KEY_PASS:-ninthloop}" \
    -dname "CN=The Ninth Loop, OU=Games, O=Blackmoor, L=Unknown, S=Unknown, C=US" \
    >/dev/null 2>&1
fi

echo "sdk.dir=$ANDROID_HOME" > android/local.properties

echo "==> assembling release APK"
( cd android && gradle --no-daemon assembleRelease )

mkdir -p dist
cp android/app/build/outputs/apk/release/app-release.apk "dist/TheNinthLoop-$VERSION.apk"

echo "==> verifying"
"$ANDROID_HOME"/build-tools/*/apksigner verify "dist/TheNinthLoop-$VERSION.apk"

echo
echo "built dist/TheNinthLoop-$VERSION.apk ($(du -h "dist/TheNinthLoop-$VERSION.apk" | cut -f1))"
echo "install with: adb install -r dist/TheNinthLoop-$VERSION.apk"
