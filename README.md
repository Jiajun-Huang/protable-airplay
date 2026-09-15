# Portable AirPlay

A portable AirPlay audio receiver written in C.

Supports traditional RAOP and AirPlay 2 audio, with the protocol stack separated from networking, OS, and audio-device implementations.

## Why this project?

Most AirPlay receiver implementations are tightly coupled to a specific OS, application, or runtime.

This project is designed to make the AirPlay stack reusable across very different targets.

* **AirPlay 1 + AirPlay 2** — supports RAOP, AirPlay 2 realtime audio, and AirPlay 2 buffered audio.
* **Portable C core** — protocol code does not depend directly on platform APIs.
* **Embedded-friendly memory model** — shared code avoids heap allocation where possible and keeps major buffers and protocol state explicitly owned.
* **Small platform interface** — porting only requires networking, OS primitives, and audio output.
* **Complete audio pipeline** — transport, decryption, decoding, buffering, clock synchronization, and playback scheduling are handled by the shared core.
* **NTP and PTP synchronization** — audio is scheduled against the sender clock instead of packet arrival time.
* **Clear module boundaries** — protocol, crypto, codecs, synchronization, services, and audio processing are kept separate.

The goal is to provide an AirPlay implementation that is easy to understand, integrate, port, and fit into systems with tighter runtime and memory constraints.

## Supported Audio

| Mode               | Transport | Codec      |
| ------------------ | --------- | ---------- |
| RAOP               | RTP / UDP | ALAC / PCM |
| AirPlay 2 Realtime | RTP / UDP | ALAC       |
| AirPlay 2 Buffered | TCP       | AAC        |

Decoded audio is exposed to the platform layer as interleaved signed 16-bit PCM.

## Architecture

```text
                         AirPlay Sender
                               │
                               ▼
                     ┌───────────────────┐
                     │       mDNS        │
                     │   RTSP / Pairing  │
                     └─────────┬─────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────┐
│                     Shared AirPlay Core                 │
│                                                         │
│  airplay   protocol   service   crypto   sync           │
│                           │                             │
│                           ▼                             │
│                    Audio Pipeline                       │
│                           │                             │
│                 codec → buffer → schedule               │
└───────────────────────────┬─────────────────────────────┘
                            │
             ┌──────────────┼──────────────┐
             ▼              ▼              ▼
          net.h           os.h          audio.h
             │              │              │
             └──────────────┼──────────────┘
                            ▼
                     Platform Backend
```

The project is split into a platform-independent AirPlay core and a small platform abstraction layer.

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

The shared layer is written with constrained systems in mind. Core state and large buffers are kept explicit instead of being hidden behind frequent dynamic allocation.

Third-party libraries may still allocate internally. A future goal is to route those allocations through configurable allocators so deployments can use fixed or statically reserved memory where practical.

### `platform/`

The shared core depends on three platform interfaces:

```text
platform/
├── net.h         TCP / UDP / multicast / polling
├── os.h          mutexes, sleeping and clock access
└── audio.h       PCM output and output-delay reporting
```

Platform backends can implement these interfaces using their own network stack, scheduler, and audio driver.

## Runtime

The receiver runs three main services:

```text
mDNS
  └── advertises the AirPlay receiver

RTSP
  └── pairing
  └── session setup
  └── RECORD / FLUSH / teardown

Audio
  └── receive packets
  └── decrypt
  └── decode
  └── synchronize
  └── schedule
  └── output PCM
```

The platform audio implementation does not need to understand AirPlay, pairing, encryption, RTP, NTP, or PTP.

## Dependencies

The shared implementation currently relies on:

* [mbedTLS](https://github.com/Mbed-TLS/mbedtls) — cryptography and pairing
* [FDK-AAC](https://github.com/mstorsjo/fdk-aac) — AAC decoding

ALAC and PCM handling are provided by the project.

Dependencies are fetched automatically by CMake through CPM.

## Build

Requirements:

* CMake 3.20+
* C11 / C++ compiler
* Ninja recommended

### Windows

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\airplay_player.exe
```

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

A specific network interface and receiver name can also be supplied:

```text
airplay_player <IPv4> <MAC> [name]
```

## Configuration

Runtime defaults and memory limits are defined in:

```text
airplay_config.h
```

This includes the receiver name, service ports, audio defaults, and buffer sizes.

## Status

The project is under active development.

The shared core already minimizes dynamic allocation. Reducing or eliminating allocations inside external libraries through configurable allocators is a planned next step.

Compatibility reports, fixes, and protocol improvements are welcome.
