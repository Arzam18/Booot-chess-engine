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
repository root. They are runtime assets and are not part of the recovered
archive. Launch from the repository root:

```bash
./cpp-port/booot-uci-android-arm64 --network-dir . --hash 16
```

The Android workflow performs the same build and packages the single
executable. Keep `verify_android_arm64.sh` beside `build_booot_android_arm64.sh`;
it is a required companion script and is included in the Stage 17 modified-only
overlay package. All helper scripts are invoked through Bash, so executable
file modes are not required. Android system libraries remain dynamically linked; libc++ is
included statically in the executable. No Android NDK binary or device runtime
result is claimed until the manual workflow and on-device tests are completed.
