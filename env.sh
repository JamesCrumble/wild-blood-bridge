#!/usr/bin/env bash
# Source this from the repository root: `source env.sh`. Touches only the current shell.
_P="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
export WB_PROBE="$_P"
export WB_REPO="$_P/port"
export WB_GAMEDIR="$_P/wbdata"
export JAVA_HOME="$_P/jdk"
export ANDROID_HOME="$_P/android-sdk"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export PKG_CONFIG_PATH="$_P/sdl-arm/lib/pkgconfig"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/build-tools/34.0.0:$ANDROID_HOME/cmdline-tools/latest/bin:$PATH"
unset _P
