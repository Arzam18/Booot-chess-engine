#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
API="${ANDROID_API:-24}"
OUTPUT="${OUTPUT:-$ROOT/booot-uci-android-arm64}"
HOST_TAG="linux-x86_64"

usage() {
  cat <<USAGE
Booot 7.4 Android ARM64 UCI build (Stage 16)

Usage:
  ANDROID_NDK_ROOT=/path/to/android-ndk-r30 ./build_booot_android_arm64.sh

Environment:
  ANDROID_NDK_ROOT   NDK root (CI pins r30; explicit local r27c also accepted)
  ANDROID_API        Android minimum API level (default: 24)
  OUTPUT             Output executable path

Target:
  aarch64-linux-android${API}
  ARMv8-A + mandatory AArch64 NEON; no dot-product-only instructions
  libc++ is linked statically; Android system libraries remain dynamic
  ELF LOAD segments are aligned for 16 KiB Android page-size devices
USAGE
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then usage; exit 0; fi

if ! [[ "$API" =~ ^[0-9]+$ ]] || (( API < 21 )); then
  echo "ERROR: ANDROID_API must be an integer >= 21 (received: $API)" >&2
  exit 2
fi

NDK_ROOT="${ANDROID_NDK_ROOT:-${ANDROID_NDK_HOME:-}}"
if [[ -z "$NDK_ROOT" ]]; then
  for candidate in \
    "${ANDROID_HOME:-}/ndk/30.0.16248370" \
    "${ANDROID_SDK_ROOT:-}/ndk/30.0.16248370" \
    "$HOME/Android/Sdk/ndk/30.0.16248370" \
    "/opt/android-sdk/ndk/30.0.16248370" \
    "${ANDROID_HOME:-}/ndk/27.2.12479018" \
    "${ANDROID_SDK_ROOT:-}/ndk/27.2.12479018" \
    "$HOME/Android/Sdk/ndk/27.2.12479018" \
    "/opt/android-sdk/ndk/27.2.12479018"; do
    if [[ -n "$candidate" && -d "$candidate" ]]; then NDK_ROOT="$candidate"; break; fi
  done
fi

if [[ -z "$NDK_ROOT" || ! -d "$NDK_ROOT/toolchains/llvm/prebuilt/$HOST_TAG/bin" ]]; then
  cat >&2 <<ERROR
ERROR: Android NDK was not found.
Set ANDROID_NDK_ROOT to the extracted Android NDK directory (CI pins r30).
Expected compiler:
  <NDK>/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android${API}-clang++
The workflow .github/workflows/android-arm64.yml downloads and verifies NDK r30.
ERROR
  exit 1
fi

TOOLCHAIN="$NDK_ROOT/toolchains/llvm/prebuilt/$HOST_TAG/bin"
CXX="$TOOLCHAIN/aarch64-linux-android${API}-clang++"
READELF="$TOOLCHAIN/llvm-readelf"
if [[ ! -x "$CXX" ]]; then echo "ERROR: Android compiler missing: $CXX" >&2; exit 1; fi
if [[ ! -x "$READELF" ]]; then echo "ERROR: llvm-readelf missing: $READELF" >&2; exit 1; fi

SOURCES=(
  booot_nnue.cpp booot_nnue_move.cpp booot_bitboard.cpp booot_attacks.cpp
  booot_hash.cpp booot_board.cpp booot_tactics.cpp booot_endgame.cpp
  booot_eval.cpp booot_move_order.cpp booot_search.cpp booot_uci.cpp
  booot_uci_main.cpp
)
for source in "${SOURCES[@]}"; do
  [[ -f "$ROOT/$source" ]] || { echo "ERROR: source file missing: $ROOT/$source" >&2; exit 1; }
done

mkdir -p "$(dirname "$OUTPUT")"
CXXFLAGS=(
  -std=c++20 -O3 -DNDEBUG
  -Wall -Wextra -Wpedantic
  -march=armv8-a+simd
  -fPIE -fstack-protector-strong -D_FORTIFY_SOURCE=2
  -ffunction-sections -fdata-sections
  -pthread -stdlib=libc++
  -I"$ROOT"
)
LDFLAGS=(
  -pie -static-libstdc++ -pthread
  -Wl,--gc-sections -Wl,--no-undefined
  -Wl,-z,relro,-z,now
  -Wl,-z,max-page-size=16384
  -Wl,--build-id=sha1
)

printf 'Booot 7.4 Android ARM64 build\n'
printf 'NDK: %s\n' "$NDK_ROOT"
printf 'Compiler: %s\n' "$CXX"
"$CXX" --version | head -n 1
printf 'API: %s\nArchitecture: armv8-a+simd (AArch64 NEON baseline)\n' "$API"
printf 'Output: %s\n' "$OUTPUT"

"$CXX" "${CXXFLAGS[@]}" "${SOURCES[@]/#/$ROOT/}" "${LDFLAGS[@]}" -o "$OUTPUT"
chmod 755 "$OUTPUT"

READELF="$READELF" "$ROOT/verify_android_arm64.sh" "$OUTPUT"
echo "Android ARM64 build successful: $OUTPUT"
echo "Required runtime network files: booot2.nn, booot4.nn, booot58.nn"
