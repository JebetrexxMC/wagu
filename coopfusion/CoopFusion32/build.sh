#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")"
if [ -n "${NDK_BUILD:-}" ]; then
    BUILD_TOOL="$NDK_BUILD"
elif [ -n "${ANDROID_NDK_HOME:-}" ] && [ -x "$ANDROID_NDK_HOME/ndk-build" ]; then
    BUILD_TOOL="$ANDROID_NDK_HOME/ndk-build"
elif [ -n "${ANDROID_NDK_ROOT:-}" ] && [ -x "$ANDROID_NDK_ROOT/ndk-build" ]; then
    BUILD_TOOL="$ANDROID_NDK_ROOT/ndk-build"
elif command -v ndk-build >/dev/null 2>&1; then
    BUILD_TOOL="$(command -v ndk-build)"
else
    echo "Android NDK not found. Set NDK_BUILD, ANDROID_NDK_HOME, or ANDROID_NDK_ROOT." >&2
    exit 127
fi
exec "$BUILD_TOOL" -C . NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk "$@"
