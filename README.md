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

They were not included in the recovered source archive, so this package does not fabricate or substitute them. The C++ executable loads them directly from the directory passed with `--network-dir`.

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

The workflow enters `cpp-port/`, compiles only the explicitly listed C++ sources, verifies the resulting AArch64 ELF, and uploads the executable. Pascal sources at repository root are not part of the compile command.

For a local NDK build:

```bash
cd cpp-port
ANDROID_NDK_ROOT=/path/to/android-ndk bash ./build_booot_android_arm64.sh
```

After building, launch from the repository root so the three network files are easy to reference:

```bash
./cpp-port/booot-uci-android-arm64 --network-dir . --hash 16
```
