#!/bin/bash
# Run in MSYS2 by install_mario64mr.ps1: install_mario64mr.py with the paths it was given (Windows ones, in the
# environment, so that no quoting is lost on the way): M64_REPO, M64_ROM, M64_WORK, and M64_ADB, M64_APK and
# M64_PUSH (empty or not).
set -e
cd "$(cygpath -u "$M64_REPO")"
if [ -n "$M64_ADB" ]; then
    export PATH="$PATH:$(cygpath -u "$M64_ADB")"
fi
args=("$(cygpath -u "$M64_ROM")" --work "$(cygpath -u "$M64_WORK")")
if [ -n "$M64_PUSH" ]; then
    args+=(--push --wait)
fi
if [ -n "$M64_APK" ]; then
    args+=(--apk "$(cygpath -u "$M64_APK")")
fi
exec python3 install_mario64mr.py "${args[@]}"
