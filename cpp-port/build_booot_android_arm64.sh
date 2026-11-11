#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$ROOT/.." && pwd)"
VERIFY_SCRIPT="$ROOT/verify_android_arm64.sh"
API="${ANDROID_API:-24}"
OUTPUT="${OUTPUT:-$ROOT/booot-uci-android-arm64}"
HOST_TAG="linux-x86_64"

usage() {
  cat <<USAGE
Booot 7.4 Android ARM64 self-contained UCI build (Stage 18)

Usage:
  ANDROID_NDK_ROOT=/path/to/android-ndk-r30 ./build_booot_android_arm64.sh

Environment:
  ANDROID_NDK_ROOT   NDK root (CI pins r30; explicit local r27c also accepted)
  ANDROID_API        Android minimum API level (default: 24)
  OUTPUT             Output executable path

Target:
  aarch64-linux-android${API}
  ARMv8-A + mandatory AArch64 NEON baseline; optional SDOT is runtime-dispatched
  only when Android reports HWCAP_ASIMDDP, with a safe NEON fallback
  The exact original booot2.nn, booot4.nn, and booot58.nn files are embedded
  from the repository root using assembler .incbin; no runtime model files are needed.
  libc++ is linked statically; Android system libraries remain dynamic
  ELF LOAD segments are aligned for 16 KiB Android page-size devices
USAGE
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then usage; exit 0; fi

# Fail early with a clear message if the modified-files overlay omitted this
# required companion script. The Android compile must not start without it.
if [[ ! -f "$VERIFY_SCRIPT" ]]; then
  echo "ERROR: required ELF verifier script is missing: $VERIFY_SCRIPT" >&2
  echo "Restore cpp-port/verify_android_arm64.sh from the Stage 17 package." >&2
  exit 1
fi

NNUE_FILES=(booot2.nn booot4.nn booot58.nn)
for model in "${NNUE_FILES[@]}"; do
  if [[ ! -s "$REPO_ROOT/$model" ]]; then
    echo "ERROR: required original NNUE file is missing or empty: $REPO_ROOT/$model" >&2
    echo "Add the three original, unmodified Booot 7.4 network files at repository root." >&2
    echo "The build will not create substitute networks or publish a non-embedded binary." >&2
    exit 1
  fi
done

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
OBJDUMP="$TOOLCHAIN/llvm-objdump"
if [[ ! -x "$CXX" ]]; then echo "ERROR: Android compiler missing: $CXX" >&2; exit 1; fi
if [[ ! -x "$READELF" ]]; then echo "ERROR: llvm-readelf missing: $READELF" >&2; exit 1; fi
if [[ ! -x "$OBJDUMP" ]]; then echo "ERROR: llvm-objdump missing: $OBJDUMP" >&2; exit 1; fi

SOURCES=(
  booot_nnue.cpp booot_nnue_move.cpp booot_bitboard.cpp booot_attacks.cpp
  booot_hash.cpp booot_board.cpp booot_tactics.cpp booot_endgame.cpp
  booot_eval.cpp booot_move_order.cpp booot_search.cpp booot_uci.cpp
  booot_uci_main.cpp booot_nnue_embedded.S
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
  -DBOOOT_EMBEDDED_NNUE=1
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
printf 'API: %s\nArchitecture: armv8-a+simd baseline + optional runtime SDOT dispatch\n' "$API"
printf 'Output: %s\n' "$OUTPUT"
printf 'Original NNUE SHA-256 values (embedded verbatim):\n'
sha256sum "${NNUE_FILES[@]/#/$REPO_ROOT/}"

mkdir -p "$(dirname "$OUTPUT")"
if [[ "$OUTPUT" != /* ]]; then
  OUTPUT="$(cd "$(dirname "$OUTPUT")" && pwd)/$(basename "$OUTPUT")"
fi
cd "$ROOT"
"$CXX" "${CXXFLAGS[@]}" "${SOURCES[@]/#/$ROOT/}" "${LDFLAGS[@]}" -o "$OUTPUT"
chmod 755 "$OUTPUT"

BOOOT_NNUE_DIR="$REPO_ROOT" READELF="$READELF" OBJDUMP="$OBJDUMP" bash "$VERIFY_SCRIPT" "$OUTPUT"
MANIFEST="$OUTPUT.nnue-manifest.txt"
{
  echo 'Booot 7.4 Android ARM64 — embedded NNUE manifest'
  echo "Executable: $(basename "$OUTPUT")"
  echo 'Network data is embedded verbatim; no .nn files are required at runtime.'
  sha256sum "${NNUE_FILES[@]/#/$REPO_ROOT/}"
} > "$MANIFEST"
echo "Android ARM64 self-contained build successful: $OUTPUT"
echo "Embedded NNUE manifest: $MANIFEST"
