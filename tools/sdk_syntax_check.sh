#!/usr/bin/env bash
# Compiles every plugin source (src/plugin/**/*.cpp) against the real BakkesMod SDK headers with clang
# in -fsyntax-only mode. It catches API misuse (wrong names, signatures, ambiguous overloads) on Linux,
# without MSVC. It does NOT link and does not replace the Windows build.
#
# usage: tools/sdk_syntax_check.sh [file.cpp ...]
# env:   BAKKESMOD_SDK_DIR (optional, otherwise the SDK is cloned at the pinned commit into .sdk/)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK_COMMIT="$(cat "$ROOT/tools/bakkesmod_sdk_commit.txt")"
WORK="$ROOT/.sdk"
CXX="${CXX:-clang++}"
mkdir -p "$WORK"

SDK="${BAKKESMOD_SDK_DIR:-$WORK/BakkesModSDK}"
if [ ! -f "$SDK/include/bakkesmod/plugin/bakkesmodplugin.h" ]; then
    rm -rf "$WORK/BakkesModSDK"
    git clone --quiet https://github.com/bakkesmodorg/BakkesModSDK.git "$WORK/BakkesModSDK"
    git -C "$WORK/BakkesModSDK" checkout --quiet "$SDK_COMMIT"
    SDK="$WORK/BakkesModSDK"
fi

STAMP="$WORK/mirror.stamp"
if [ ! -f "$STAMP" ] || [ "$(cat "$STAMP")" != "$SDK" ]; then
    python3 "$ROOT/tools/sdk_case_mirror.py" "$SDK/include" "$WORK/mirror"
    rm -rf "$WORK/imgui" && mkdir -p "$WORK/imgui_zip"
    python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$SDK/imgui_bm.zip" "$WORK/imgui_zip"
    mv "$WORK/imgui_zip/imgui" "$WORK/imgui" && rm -rf "$WORK/imgui_zip"
    echo "$SDK" > "$STAMP"
fi

INCLUDES=()
while IFS= read -r d; do INCLUDES+=("-I$d"); done < <(find "$WORK/mirror" -type d)
INCLUDES+=("-I$WORK/imgui" "-I$ROOT/src" "-I$ROOT")

if [ "$#" -gt 0 ]; then
    FILES=("$@")
else
    mapfile -t FILES < <(find "$ROOT/src/plugin" -name '*.cpp' | sort)
fi

FAILED=0
for f in "${FILES[@]}"; do
    if ! "$CXX" -std=c++20 -fsyntax-only -fms-extensions -fdeclspec -Wno-everything \
        -DNOMINMAX -DWIN32_LEAN_AND_MEAN -include "$ROOT/tools/sdk_prelude.h" "${INCLUDES[@]}" "$f"; then
        echo "FAILED: $f"
        FAILED=$((FAILED + 1))
    fi
done
echo "sdk_syntax_check: ${#FILES[@]} files, $FAILED failed"
[ "$FAILED" -eq 0 ]
