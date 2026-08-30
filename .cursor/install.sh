#!/usr/bin/env bash
#
# Cloud Agent environment bootstrap for scrcpy (client + server).
#
# Installs the C client build dependencies (Meson/Ninja, SDL2, FFmpeg, libusb),
# the Android SDK needed to build the Java server, then builds both.
#
# Idempotent: safe to re-run. Run as a non-root user with sudo available.
set -euo pipefail

ANDROID_HOME="${ANDROID_HOME:-$HOME/android-sdk}"
ANDROID_SDK_ROOT="$ANDROID_HOME"
CMDLINE_TOOLS_URL="https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip"
ANDROID_PLATFORM="platforms;android-36"
ANDROID_BUILD_TOOLS="build-tools;36.0.0"

echo "==> Installing client build dependencies (apt)"
sudo apt-get update -qq
sudo apt-get install -y --no-install-recommends \
    gcc git pkg-config meson ninja-build \
    libsdl2-dev libavcodec-dev libavdevice-dev libavformat-dev \
    libavutil-dev libswresample-dev libusb-1.0-0-dev \
    adb curl unzip default-jdk-headless

echo "==> Installing Android SDK command-line tools (for the server build)"
export ANDROID_HOME ANDROID_SDK_ROOT
if [ ! -x "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" ]; then
    mkdir -p "$ANDROID_HOME/cmdline-tools"
    tmp="$(mktemp -d)"
    curl -fsSL -o "$tmp/cmdline-tools.zip" "$CMDLINE_TOOLS_URL"
    unzip -q "$tmp/cmdline-tools.zip" -d "$ANDROID_HOME/cmdline-tools"
    mv "$ANDROID_HOME/cmdline-tools/cmdline-tools" "$ANDROID_HOME/cmdline-tools/latest"
    rm -rf "$tmp"
fi
export PATH="$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH"

echo "==> Accepting licenses and installing SDK packages"
yes | sdkmanager --licenses >/dev/null 2>&1 || true
sdkmanager "platform-tools" "$ANDROID_PLATFORM" "$ANDROID_BUILD_TOOLS"

echo "==> Persisting Android SDK environment for interactive shells"
PROFILE="$HOME/.bashrc"
if ! grep -q "ANDROID_HOME=$ANDROID_HOME" "$PROFILE" 2>/dev/null; then
    {
        echo ""
        echo "# scrcpy Android SDK (added by .cursor/install.sh)"
        echo "export ANDROID_HOME=$ANDROID_HOME"
        echo "export ANDROID_SDK_ROOT=$ANDROID_HOME"
        echo "export PATH=\$ANDROID_HOME/cmdline-tools/latest/bin:\$ANDROID_HOME/platform-tools:\$ANDROID_HOME/emulator:\$PATH"
    } >> "$PROFILE"
fi

# Let Gradle locate the SDK (local.properties is gitignored).
echo "sdk.dir=$ANDROID_HOME" > "$(dirname "$0")/../local.properties"

echo "==> Building scrcpy client + server (meson/ninja)"
if [ -d build ]; then
    meson setup build --buildtype=release --reconfigure
else
    meson setup build --buildtype=release
fi
ninja -C build

echo "==> Done. Run with: ./run build [scrcpy options]"
