# Portable AirPlay Speaker

[![CMake](https://img.shields.io/badge/build-CMake%203.20%2B-064F8C?logo=cmake&logoColor=white)](https://cmake.org/)
[![Language](https://img.shields.io/badge/language-C11-1f6feb?logo=c&logoColor=white)](<https://en.wikipedia.org/wiki/C11_(C_standard_revision)>)
[![GitHub stars](https://img.shields.io/github/stars/Jiajun-Huang/airplay?style=flat&logo=github)](https://github.com/Jiajun-Huang/airplay/stargazers)
[![GitHub issues](https://img.shields.io/github/issues/Jiajun-Huang/airplay)](https://github.com/Jiajun-Huang/airplay/issues)

**A portable, embedded-friendly AirPlay receiver written in C.**

Receive audio from an iPhone, iPad, or Mac with a small protocol core that can run on Windows, Linux, macOS, and custom embedded platforms. Classic RAOP (AirPlay 1) and AirPlay 2 share the same buffering, decoding, synchronization, and platform interfaces.

<p align="center"><a href="#quick-start">Build it</a> · <a href="#supported-audio">See supported audio</a> · <a href="#porting-to-a-new-target">Port it</a> · <a href="https://github.com/Jiajun-Huang/airplay/issues">Report an issue</a></p>

## Highlights

- AirPlay 1 RAOP and AirPlay 2 realtime and buffered audio
- Portable C core with a small platform abstraction
- ALAC, AAC, and PCM decoding into interleaved signed 16-bit PCM
- Encrypted audio, pairing, RTP buffering, and RTSP session management
- NTP/PTP clock synchronization and sender-clock-based playback scheduling
- Explicit buffers and protocol state suitable for desktop and embedded targets

The core is designed to be understandable, portable, and practical on systems with tighter memory and runtime constraints.

> If this project is useful to you, a Star helps other developers find it and helps guide future protocol and platform work.

## Supported Audio

| Mode               | Transport | Codec      |
| ------------------ | --------- | ---------- |
| RAOP               | RTP / UDP | ALAC / PCM |
| AirPlay 2 Realtime | RTP / UDP | ALAC       |
| AirPlay 2 Buffered | TCP       | AAC        |

Decoded audio is exposed to the platform layer as interleaved signed 16-bit PCM.

## Architecture

The project is split into a platform-independent AirPlay core and a small platform abstraction layer. The runtime starts three services: mDNS discovery, RTSP/session control, and audio processing.

### `shared/`

```text
shared/
├── airplay/      AirPlay session state
├── audio/        packet buffering and playback pipeline
├── codec/        ALAC, AAC and PCM decoding
├── crypto/       pairing, FairPlay and audio decryption
├── protocol/     RTSP, RTP, SDP and protocol parsing
├── service/      mDNS, RTSP and audio services
├── sync/         NTP / PTP clock synchronization
└── util/         shared utilities
```

The shared layer keeps core state and large buffers explicit instead of hiding them behind frequent dynamic allocation. Third-party libraries may still allocate internally; configurable allocators are a future portability goal.

### `platform/`

The shared core depends on three platform interfaces:

```text
platform/
├── net.h         TCP / UDP / multicast / polling
├── os.h          mutexes, sleeping and clock access
└── audio.h       PCM output and output-delay reporting
```

Platform backends can implement these interfaces using their own network stack, scheduler, and audio driver.

The platform audio implementation only needs to provide PCM output and output-delay reporting. It does not need to understand AirPlay, pairing, encryption, RTP, NTP, or PTP.

## Dependencies

The shared implementation currently relies on:

- [mbedTLS](https://github.com/Mbed-TLS/mbedtls) — cryptography and pairing
- [FDK-AAC](https://github.com/mstorsjo/fdk-aac) — AAC decoding

ALAC and PCM handling are provided by the project.

Dependencies are fetched automatically by CMake through CPM.

## Quick start

### Windows

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\airplay_player.exe
```

The receiver discovers an active IPv4 adapter automatically. To select an interface and name explicitly:

```powershell
.\build\airplay_player.exe 192.168.1.20 AABBCCDDEEFF LivingRoom
```

The arguments are `IPv4 MAC_HEX [NAME]`; `MAC_HEX` must contain 12 hexadecimal characters. Open AirPlay on the sender and select the receiver from the audio output menu.

### Linux

```bash
cmake -S . -B build -G Ninja \
    -DAIRPLAY_PLATFORM=linux \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/airplay_player
```

### macOS

```bash
cmake -S . -B build -G Ninja \
    -DAIRPLAY_PLATFORM=apple \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/airplay_player
```

Press `Ctrl+C` to stop the receiver.

## Build requirements

Requirements:

- CMake 3.20+
- A C11 compiler
- Ninja recommended

## Test locally

Desktop builds include protocol, network-contract, server-lifecycle, RTSP, mDNS, ALAC, timing, AirPlay 2, and buffered-audio tests.

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests are disabled for `AIRPLAY_PLATFORM=embedded` because embedded board projects provide their own runtime and test harness.

## Configuration

Runtime defaults and memory limits are defined in:

```text
airplay_config.h
```

This includes the receiver name, service ports, audio defaults, and buffer sizes.

The default service ports are RTSP `5000`, realtime/control/timing UDP `6000`/`6001`/`6002`, and mDNS `5353`. Rebuild after changing compile-time settings.

## Porting to a new target

1. Implement `platform/net.h`, `platform/os.h`, and `platform/audio.h` for the target.
2. Add a platform directory and its `CMakeLists.txt` under `platform/`.
3. Configure CMake with `-DAIRPLAY_PLATFORM=<target>`.
4. Set board include paths with `-DAIRPLAY_PLATFORM_INCLUDE_DIRS="..."` when required.

The `embedded` backend is intentionally a board-integration starting point: it expects the board project to provide the audio and network environment.

## Contributing

Useful contributions include:

- Testing with real AirPlay senders and reporting the sender model, platform, and logs
- Porting the three platform interfaces to a new board or operating system
- Improving protocol compatibility, timing behavior, and test fixtures
- Documenting reproducible build and integration steps

Please open an issue before large protocol or platform changes. Small, focused pull requests are easiest to review.

## Repository links

- [Issue tracker](https://github.com/Jiajun-Huang/airplay/issues)
- [Discussions and questions](https://github.com/Jiajun-Huang/airplay/discussions)
- [Project source](https://github.com/Jiajun-Huang/airplay)

## Status

The project is under active development. The desktop backends are intended for local development and validation; embedded deployments still need board-specific integration and tuning.

The shared core already minimizes dynamic allocation. Reducing or eliminating allocations inside external libraries through configurable allocators is a planned next step.

Compatibility reports, fixes, tests, and protocol improvements are welcome.
