# macOS arm64 Release Build

This note covers the current minimal macOS arm64 release build and TestUnits path.

## Verified Local Environment

Validated locally on macOS arm64 with the following toolchain and Homebrew dependencies:

```sh
/opt/homebrew/bin/cmake --version
# cmake version 4.3.4

c++ --version
# Apple clang version 21.0.0 (clang-2100.1.1.101)
# Target: arm64-apple-darwin25.5.0

brew --prefix
# /opt/homebrew

brew list --versions boost fmt nanomsg rapidjson spdlog zeromq cppzmq
# boost 1.90.0_1
# fmt 12.2.0
# nanomsg 1.2.2
# rapidjson 1.1.0
# spdlog 1.17.0
# zeromq 4.3.5_2
```

`cppzmq` is not installed in the verified Homebrew environment above. The default CMake setting is `WT_ENABLE_ZMQ=AUTO`, so `ParserZMQ` and `TraderZMQ` are skipped when `zmq.hpp`/cppzmq is missing. This does not verify the ZMQ adapters.

## Configure And Build

From the repository root, use the release helper:

```sh
CMAKE_BIN=/opt/homebrew/bin/cmake WT_BUILD_JOBS=4 src/build_release.sh
```

Equivalent direct CMake commands:

```sh
/opt/homebrew/bin/cmake -S src -B src/build_all -DCMAKE_BUILD_TYPE=Release
/opt/homebrew/bin/cmake --build src/build_all --config Release --parallel 4
```

The arm64 release binaries are written under `src/build_all/build_arm64/Release`.

## wtpy Wrapper Packaging

After building, copy the available macOS arm64 wrapper libraries into a wtpy
package root:

```sh
./copy_bins_macos_arm64.sh [path-to-wtpy-root]
```

When `[path-to-wtpy-root]` is omitted, the script uses `../wtpy`, matching the
default target convention used by `copy_bins_linux.sh`. The copied wrapper
artifacts are placed under:

```text
<path-to-wtpy-root>/wtpy/wrapper/darwin
```

The script copies only `.dylib` files that exist in the current build under
`src/build_all/build_arm64/Release/bin`. Top-level wrapper libraries are copied
to `wtpy/wrapper/darwin`, and one-level plugin directories such as `executer`,
`parsers`, and `traders` are copied under the same relative subdirectory names.
Missing optional component directories are skipped with a message.

This packaging step does not deploy third-party CTP SDK `.dylib` files and does
not validate ZMQ adapters. With the default `WT_ENABLE_CTP=AUTO` and
`WT_ENABLE_ZMQ=AUTO`, those artifacts are copied only when the corresponding
targets were actually built.

## TestUnits

Build and run `TestUnits` from the repository root:

```sh
/opt/homebrew/bin/cmake --build src/build_all --config Release --target TestUnits --parallel 4
src/build_all/build_arm64/Release/bin/TestUnits/TestUnits
```

`TestUnits` is non-interactive and returns a non-zero status when a test fails.

## CTP

The CTP binaries currently committed under `src/API/CTP6.3.15` are Linux x86_64 `.so` files and Windows `.dll` files, not macOS arm64 libraries. With the default `WT_ENABLE_CTP=AUTO`, CMake skips CTP targets when a usable macOS arm64 CTP SDK is not available.

To enable CTP on macOS arm64, provide an external macOS arm64 CTP SDK and configure explicitly:

```sh
/opt/homebrew/bin/cmake -S src -B src/build_all \
  -DCMAKE_BUILD_TYPE=Release \
  -DWT_ENABLE_CTP=ON \
  -DCTP_ROOT=/path/to/macos-arm64/ctp-sdk
```

If direct/static SDK linkage is required, also pass `-DWT_CTP_STATIC=ON`.

## Status

The environment above verified the original minimal macOS arm64 path. The
subsequent portability rebuild has not yet been rerun on macOS, so Linux
validation of that branch must not be treated as current macOS evidence.

Completed for macOS arm64:

- CMake arm64 platform selection and output layout.
- Asio/fmt build compatibility with Homebrew dependencies.
- Optional CTP build gating through `WT_ENABLE_CTP=AUTO`, `WT_ENABLE_CTP=ON`, `CTP_ROOT`, and `WT_CTP_STATIC`.
- Release helper script using portable `sh` and configurable CMake path/job count.
- Minimal macOS arm64 wtpy wrapper packaging via `copy_bins_macos_arm64.sh`.

Likely follow-up milestones:

- Code signing/notarization.
- Runtime plugin deployment layout.
