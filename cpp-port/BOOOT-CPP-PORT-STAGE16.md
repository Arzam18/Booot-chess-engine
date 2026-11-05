# Booot 7.4 C++ Port — Stage 16: Android ARM64 Build Integration (Restructured)

## Scope

Stage 16 carries the Stage 15 UCI implementation forward and adds a reproducible
Android ARM64 executable build path. This reissued package also applies the
repository layout migration:

- Original Pascal project/source files are preserved at repository root.
- All C++ port source, tests, build scripts, and documentation are under
  `cpp-port/`.
- The active C++ workflow remains at repository-root
  `.github/workflows/android-arm64.yml`.
- The original Pascal workflow is archived as `original_pascal_android.yml` and
  is not an active workflow.

## Android build configuration

| Setting | Value |
|---|---|
| NDK | r30 / 30.0.16248370 (CI pin); local r27c accepted |
| ABI | arm64-v8a |
| Compiler target | aarch64-linux-android24 |
| Minimum API | 24 |
| ISA baseline | `-march=armv8-a+simd` |
| Language | C++20 |
| Optimization | `-O3 -DNDEBUG` |
| C++ runtime | Static libc++ |
| Executable | `cpp-port/booot-uci-android-arm64` |
| ELF | PIE, 16 KiB PT_LOAD alignment, RELRO and NOW |

The executable remains dynamically linked against Android Bionic system
libraries. Static libc++ removes the separate `libc++_shared.so` dependency; it
does not make the executable fully static.

## Workflow isolation

The repository-root workflow is `workflow_dispatch` only. It checks out the
repository, downloads and verifies NDK r30, and sets `working-directory: cpp-port`
for both build and ELF verification. The build script uses an explicit C++
translation-unit list. It does not compile root-level `.pas`, `.dpr`, `.rc`, or
`.res` files. The artifact is uploaded from `cpp-port/`.

## Original Pascal preservation

All 23 files from the prior `original_pascal/` directory were restored to root
(or retained as named root reference files for the original README/workflow).
Their bytes were compared against the prior Stage 16 archive and match exactly.
No Pascal source file was edited. The original workflow is stored as
`original_pascal_android.yml`, outside `.github/workflows/`, so it cannot be
triggered as an active workflow.

## NNUE network location

The three original network files belong in the repository root:

- `booot2.nn`
- `booot4.nn`
- `booot58.nn`

They were absent from the recovered archive and are not included or fabricated.
Launch the executable from repository root using:

```bash
./cpp-port/booot-uci-android-arm64 --network-dir . --hash 16
```

## Validation performed

- Stage 16 integration tests with GCC: PASS.
- Stage 16 integration tests with Clang 17: PASS.
- Cumulative Stage 1–15 quick host regression: PASS (deep perft skipped).
- Original Pascal files restored byte-for-byte: PASS (23 files).
- Repository structure assertions: PASS (Pascal source at root; none in cpp-port).
- Complete source archive extraction and 147-entry checksum verification: PASS.
- Modified-only migration archive extraction and structure verification: PASS.
- Archive SHA-256 manifests: PASS.
- Android NDK cross-compilation and phone runtime: NOT RUN; requires manual CI.

## Not claimed / outstanding

No Android ELF or phone runtime result is claimed. The local environment did not
have an NDK installed and could not resolve Google's download host. Run the
manual GitHub Actions workflow, inspect the resulting AArch64 ELF, then test
`uci`, `isready`, `position`, `go depth`, `stop`, and process exit in Termux.
No original-network evaluation parity can be claimed until all three original
network files are supplied and validated.
