# Android ARM64 build quick reference (Stage 16 restructured layout)

The original Pascal sources and project files stay at repository root. The C++
port is isolated in `cpp-port/`. The active workflow is at the repository-root
`.github/workflows/android-arm64.yml` and builds only from `cpp-port/`.

```bash
# From the repository root, on Linux after extracting the Android NDK:
cd cpp-port
export ANDROID_NDK_ROOT="$HOME/Android/Sdk/android-ndk-r30"
bash ./build_booot_android_arm64.sh

# Verify with the NDK's readelf:
READELF="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf" \
  bash ./verify_android_arm64.sh ./booot-uci-android-arm64
```

Place the original `booot2.nn`, `booot4.nn`, and `booot58.nn` files in the
repository root. They were not present in the recovered original source archive.
For a **host/debug build**, the executable loads those files from the directory
passed with `--network-dir` (default `.`):

```bash
cd cpp-port
./build_booot_uci.sh
cd ..
./cpp-port/booot-uci --network-dir . --hash 16
```

Keep `verify_android_arm64.sh` beside `build_booot_android_arm64.sh`; it is a
required companion script. All Actions helper scripts are invoked through Bash,
so executable file modes are not required. Android system libraries remain
dynamically linked; libc++ is included statically in the executable.

## Stage 18: self-contained Android ARM64 NNUE build

The Android build requires the original, unmodified `../booot2.nn`,
`../booot4.nn`, and `../booot58.nn` files. It embeds them into the executable
with `.incbin`, then verifies all three ELF symbols and exact byte sizes against
the source files. No external network files are needed on the phone. The
workflow uploads the executable and a `.nnue-manifest.txt` sidecar with the
SHA-256 hashes of the original model files. The executable can be launched from
any working directory without `--network-dir`.

The recovered source archive did not contain the three production networks.
The build now fails early when any one is absent or empty; it will not publish a
smaller, non-embedded binary. The Android NDK build and DroidFish runtime still
require confirmation from the manual Actions run and the actual device.
