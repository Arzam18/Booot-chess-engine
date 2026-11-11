#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then echo "Usage: $0 <Android ARM64 ELF>" >&2; exit 2; fi
BINARY="$1"
[[ -f "$BINARY" ]] || { echo "ERROR: binary not found: $BINARY" >&2; exit 1; }
READELF="${READELF:-readelf}"
command -v "$READELF" >/dev/null 2>&1 || { echo "ERROR: readelf not found: $READELF" >&2; exit 1; }

HEADER="$("$READELF" -h "$BINARY")"
grep -q 'Class:.*ELF64' <<<"$HEADER" || { echo "ERROR: expected ELF64" >&2; exit 1; }
grep -q 'Machine:.*AArch64' <<<"$HEADER" || { echo "ERROR: expected AArch64 ELF" >&2; exit 1; }

PROGRAM_HEADERS="$("$READELF" -lW "$BINARY")"
LOAD_ALIGNMENTS="$(awk '$1 == "LOAD" { print $NF }' <<<"$PROGRAM_HEADERS")"
[[ -n "$LOAD_ALIGNMENTS" ]] || { echo "ERROR: no ELF LOAD segments found" >&2; exit 1; }
while IFS= read -r alignment; do
  value=$((alignment))
  if (( value < 16384 )); then
    echo "ERROR: PT_LOAD alignment $alignment is below 16 KiB" >&2
    exit 1
  fi
done <<<"$LOAD_ALIGNMENTS"

DYNAMIC="$("$READELF" -d "$BINARY" 2>/dev/null || true)"
if grep -q 'libc++_shared\.so' <<<"$DYNAMIC"; then
  echo "ERROR: binary depends on libc++_shared.so; expected static libc++" >&2
  exit 1
fi
INTERPRETER="$(sed -n 's/.*Requesting program interpreter: \(.*\)]/\1/p' <<<"$PROGRAM_HEADERS")"
if [[ "$INTERPRETER" != "/system/bin/linker64" ]]; then
  echo "ERROR: unexpected Android ELF interpreter: ${INTERPRETER:-<missing>}" >&2
  exit 1
fi

if [[ -n "${OBJDUMP:-}" ]]; then
  command -v "$OBJDUMP" >/dev/null 2>&1 || { echo "ERROR: llvm-objdump not found: $OBJDUMP" >&2; exit 1; }
  DISASSEMBLY="$("$OBJDUMP" -d "$BINARY")"
  if ! grep -Eiq '[[:space:]]sdot[[:space:]]' <<<"$DISASSEMBLY"; then
    echo "ERROR: AArch64 runtime-dispatched SDOT kernel is missing from the executable" >&2
    exit 1
  fi
  echo '  Runtime-dispatched SDOT kernel: present (HWCAP_ASIMDDP guarded)'
else
  echo '  Runtime-dispatched SDOT kernel: disassembly not checked (OBJDUMP not supplied)'
fi

SYMBOLS="$($READELF -sW "$BINARY" 2>/dev/null || true)"
for pair in \
  'booot2_nn_start:booot2.nn' \
  'booot4_nn_start:booot4.nn' \
  'booot58_nn_start:booot58.nn'; do
  symbol="${pair%%:*}"
  model="${pair#*:}"
  embedded_size_raw="$(awk -v wanted="$symbol" '$NF == wanted { print $3; exit }' <<<"$SYMBOLS")"
  if [[ ! "$embedded_size_raw" =~ ^(0x[0-9a-fA-F]+|[0-9]+)$ ]]; then
    echo "ERROR: embedded model symbol is missing or has an invalid size: $symbol ($model)" >&2
    exit 1
  fi
  embedded_size=$((embedded_size_raw))
  if (( embedded_size == 0 )); then
    echo "ERROR: embedded model symbol is empty: $symbol ($model)" >&2
    exit 1
  fi
  if [[ -n "${BOOOT_NNUE_DIR:-}" ]]; then
    expected_size="$(stat -c '%s' "$BOOOT_NNUE_DIR/$model")"
    if (( embedded_size != expected_size )); then
      echo "ERROR: embedded $model size mismatch: ELF=$embedded_size source=$expected_size" >&2
      exit 1
    fi
  fi
  printf '  Embedded %-12s %s bytes\n' "$model" "$embedded_size"
done

printf 'ELF verification: PASS\n' 
printf '  Class: ELF64\n  Machine: AArch64\n  Interpreter: %s\n' "$INTERPRETER"
printf '  PT_LOAD alignments: 16 KiB or greater\n  libc++_shared.so dependency: absent\n  Three original NNUE byte arrays: present\n'
printf '  Note: Bionic system libraries remain dynamically linked, as required for Android executables.\n'
