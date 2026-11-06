# Booot 7.4 C++ Port — Stage 18: embedded NNUE and UCI startup repair

## Findings from Stage 17 artifact review

The Stage 17 Android build did not embed any network data. Its build script compiled only `.cpp` files, its executable opened `booot2.nn`, `booot4.nn`, and `booot58.nn` from the process working directory, and the workflow uploaded only that small executable. The executable's `main()` returned exit code 1 before constructing the UCI protocol when any model could not be opened. This explains why DroidFish could report “Failed to start engine” without ever receiving `id name` / `uciok`.

## Changes

- Android build now requires the three original, non-empty `.nn` files at the repository root. A missing model is a hard CI error; no binary without the real networks is published.
- `booot_nnue_embedded.S` uses `.incbin` to embed each original file byte-for-byte into separate read-only ELF sections.
- The C++ loader accepts a read-only memory range and parses the exact existing version-35 network format without modifying the bytes.
- `booot_uci_main.cpp` loads the embedded networks in Android builds. For host/debug builds, external `--network-dir` loading remains available.
- Startup no longer aborts before UCI handshake if host/debug network files are missing. It still answers `uci` and `isready`, emits the engine identity, and reports `bestmove 0000` with an explanatory `info string` if a `go` command is sent without weights. Search is explicitly disabled in that mode; no zero-weight or synthetic search is performed.
- ELF verification now requires all three embedded symbols and can compare their byte sizes against the original root model files.
- Actions uploads the self-contained executable and a sidecar manifest containing the original model SHA-256 hashes.

## Required repository inputs

Place the original, unmodified files at repository root:

- `booot2.nn`
- `booot4.nn`
- `booot58.nn`

These model files were not present in the recovered source archive used for this Stage 18 workspace. The local tests must use temporary synthetic fixtures only; those fixtures are never included in delivery ZIPs and never represent production weights.

## Validation

Stage 18 tests cover the memory-backed parser against the existing file parser, UCI handshake/readiness without networks, no-search behavior without weights, and a temporary fixture build proving the linker embeds all three byte ranges at exact input sizes. Android NDK build and actual Galaxy A16/DroidFish runtime remain gated on the user's Actions run and device test.
