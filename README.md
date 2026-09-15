# Portable AirPlay Speaker

A portable AirPlay audio receiver written in C. The project supports classic RAOP (AirPlay 1) and AirPlay 2 while keeping protocol logic separate from networking, operating-system services, and audio output.

## Highlights

- AirPlay 1 RAOP and AirPlay 2 realtime and buffered audio
- Portable C core with a small platform abstraction
- ALAC, AAC, and PCM decoding into interleaved signed 16-bit PCM
- Encrypted audio, pairing, RTP buffering, and RTSP session management
- NTP/PTP clock synchronization and sender-clock-based playback scheduling
- Explicit buffers and protocol state suitable for desktop and embedded targets

The core is designed to be understandable, portable, and practical on systems with tighter memory and runtime constraints.

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

## Build and run

Requirements:

- CMake 3.20+
- A C11 compiler
- Ninja recommended

### Windows

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\airplay_player.exe
```

The Windows executable detects an active IPv4 adapter automatically. To select an interface explicitly:

```powershell
.\build\airplay_player.exe 192.168.1.20 AABBCCDDEEFF LivingRoom
```

The arguments are `IPv4 MAC_HEX [NAME]`; `MAC_HEX` must contain 12 hexadecimal characters.

### Linux

```bash
cmake -S . -B build \
    -G Ninja \
    -DAIRPLAY_PLATFORM=linux \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build
./build/airplay_player
```

### macOS

```bash
cmake -S . -B build \
    -G Ninja \
    -DAIRPLAY_PLATFORM=apple \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build
./build/airplay_player
```

For Linux and macOS, use the same optional arguments with the platform executable:

```text
airplay_player <IPv4> <MAC_HEX> [NAME]
```

Press `Ctrl+C` to stop the receiver.

## Test

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

## Status

The project is under active development. The desktop backends are intended for local development and validation; embedded deployments still need board-specific integration and tuning.

The shared core already minimizes dynamic allocation. Reducing or eliminating allocations inside external libraries through configurable allocators is a planned next step.

Compatibility reports, fixes, tests, and protocol improvements are welcome.
