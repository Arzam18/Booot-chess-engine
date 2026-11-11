# Booot 7.4 — Original Pascal Source and C++ Android Port

This repository deliberately keeps the original Booot Pascal project at the repository root and isolates the C++ port in `cpp-port/`.

## Repository layout

- `booot7.dpr`, `u*.pas`, `booot7.dproj`, `booot7.lpi`, `resource.rc`, and `booot7.res`: preserved original Pascal project/source files.
- `cpp-port/`: C++20 port, UCI implementation, host tests, Android build scripts, and stage documentation.
- `.github/workflows/android-arm64.yml`: manual GitHub Actions workflow. It builds only the C++ sources inside `cpp-port/`; it does not compile Pascal files from the repository root.
- `original_pascal_android.yml`: archived copy of the original Pascal workflow, retained as reference and not used by the C++ build.

## Original NNUE networks

Place the three original, unmodified network files in this repository root:

- `booot2.nn`
- `booot4.nn`
- `booot58.nn`

They were not included in the recovered source archive, so this package does not fabricate or substitute them. The Android workflow requires all three original files and embeds their bytes verbatim into the ELF with assembler `.incbin`. It verifies that each embedded symbol has exactly the same size as the corresponding root file. The resulting Android executable does not need external `.nn` files at runtime.

## Search and NNUE implementation (Stages 19–20)

Stage 19 provides compact board undo records, incremental NNUE move updates,
fixed-capacity move-order buffers, and an optional ARMv8.2-A SDOT first-layer
kernel. SDOT is selected only when Android reports `HWCAP_ASIMDDP`; otherwise
the ARMv8-A NEON fallback is retained. The UCI handshake reports the active
first-layer kernel.

Stage 20 corrects search-fidelity differences found by comparing the C++ port
with the preserved Pascal source: TT static evaluations remain raw until they
are scaled for the current position, and LMR history weighting, signed
reductions, cut-node/PV adjustments, and re-search depth handling now follow
the Pascal search path more closely. Host regression tests pass, but this is
not yet a Pascal-vs-C++ strength-parity or Galaxy A16 playing-strength claim.
See `cpp-port/BOOOT-CPP-PORT-STAGE20.md` for details and verification.

## Build the host UCI executable

```bash
cd cpp-port
bash ./build_booot_uci.sh
cd ..
./cpp-port/booot-uci --network-dir . --hash 16
```

## Build Android ARM64

Shell scripts are invoked through `bash` so the build does not depend on an
executable permission bit being preserved by a GitHub web upload or archive.


Run the repository-root GitHub Actions workflow manually:

`Actions` → `Booot Android ARM64` → `Run workflow`.

The workflow enters `cpp-port/`, compiles only the explicitly listed C++ sources plus the NNUE embedding assembly, verifies the resulting AArch64 ELF and all three embedded model sizes, and uploads the executable plus a SHA-256 manifest. Pascal sources at repository root are not part of the compile command.

For a local NDK build:

```bash
cd cpp-port
ANDROID_NDK_ROOT=/path/to/android-ndk bash ./build_booot_android_arm64.sh
```

After building, the self-contained Android executable can be launched from any working directory; no external network path is needed:

```bash
./cpp-port/booot-uci-android-arm64 --hash 16
```
