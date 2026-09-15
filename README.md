# AirPlay Speaker

A portable C AirPlay audio receiver for computers and embedded systems. It supports traditional RAOP over UDP and AirPlay 2 realtime ALAC over UDP or buffered AAC over TCP. Audio is decrypted, decoded, and scheduled against the sender's NTP or PTP clock. One audio stream is active at a time.

## Portable library

The shared AirPlay receiver is designed as a portable C library. Protocol handling,
packet buffering, decoding orchestration, clock mapping, pairing, and playback
scheduling do not call operating-system APIs directly. To port the library to a
new platform, implement the contracts in:

- `platform/net.h`: TCP/UDP sockets, multicast, polling, and network lifecycle.
- `platform/os.h`: mutexes, sleeping, and a UTC microsecond clock.
- `platform/audio.h`: open an output device, write interleaved 16-bit PCM, report
  queued output delay, and close the device.

The platform entry point must initialize networking, create an `airplay_config_t`,
start the three service threads or tasks, and stop them before releasing the
platform resources. The existing Windows, Apple, and Linux directories are
reference implementations. Windows is the implementation currently tested by
the project; Apple and Linux builds have not been validated by us yet. Volunteers
are welcome to test those backends and open a pull request with fixes or test
results.

The embedded backend shows the same approach with FreeRTOS, lwIP, and board-owned
audio and clock functions. A different RTOS, socket stack, or audio driver can be
used as long as it satisfies the three platform interfaces above.

## Dependencies

The project keeps external dependencies focused on the parts that are difficult
to implement safely in the shared core:

- [mbedTLS 2.28.8](https://github.com/Mbed-TLS/mbedtls) provides AES-128, RSA,
  SHA-1, Base64, entropy, and random-number support used by AirPlay encryption,
  FairPlay, and pairing. Only the static `mbedcrypto` target is used.
- [FDK-AAC 2.0.3](https://github.com/mstorsjo/fdk-aac) decodes AAC streams used by
  AirPlay 2 buffered audio. ALAC decoding is provided by the project's own
  adapter and does not require a separate codec package.
- The C standard library and the selected platform SDK provide the remaining
  runtime, socket, threading, and audio facilities.

CPM fetches these dependencies during CMake configuration. They are linked
statically; the shared library does not require a separate runtime service.

## Runtime architecture

The receiver starts three independent services:

| Thread or task       | Responsibility                                                                                                                                               |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `airplay_mdns_main`  | Owns mDNS discovery traffic and advertises the receiver as an AirPlay speaker.                                                                               |
| `airplay_rtsp_main`  | Owns the RTSP control connection, handles setup/record/flush requests, and publishes a mutex-protected session snapshot.                                     |
| `airplay_audio_main` | Reads the session snapshot, receives audio/control/timing traffic, buffers and schedules packets, decodes them, and writes PCM to the platform audio device. |

### Audio pipeline

The audio pipeline converts network packets into timed PCM output:

1. RTP or buffered TCP audio is received and placed into the playout queue.
2. AirPlay encryption is removed when required.
3. ALAC, AAC, or PCM payloads are decoded into interleaved signed 16-bit PCM.
4. The stream volume is applied and the platform `audio_write()` callback is called.
5. Packet timestamps and the platform's queued-frame delay determine when a packet is submitted.

Traditional RAOP uses UDP RTP audio. AirPlay 2 realtime uses encrypted UDP audio,
and AirPlay 2 buffered mode uses the buffered TCP transport. The pipeline hides
these transport differences from the platform audio backend.

### Codecs

The SDP session announces the codec and audio format. The pipeline supports:

- ALAC (Apple Lossless), decoded by the in-tree ALAC adapter.
- AAC, decoded through FDK-AAC for AirPlay 2 buffered streams.
- Linear PCM, decoded directly from RTP network byte order.

The decoded format exposed to every platform is interleaved signed 16-bit PCM.

### Clock synchronization

Playback cannot be scheduled from packet arrival time alone. RTP control packets
provide an RTP-to-sender-clock anchor. Traditional RAOP then uses NTP timing
exchanges to measure sender/local clock offset. AirPlay 2 uses PTP and sender
anchors. The pipeline converts each RTP timestamp into a local playback deadline,
keeps packets queued until that deadline, and accounts for audio frames already
queued in the platform output device.

### Pairing and encryption

Pairing authenticates the sender and establishes the cryptographic material needed
for protected audio sessions. The AirPlay/FairPlay code handles the protocol
exchange, while the crypto layer unwraps session keys and initializes AES for
audio decryption. RTSP session setup supplies the resulting format and key data
to the audio pipeline; the platform backend does not need to know about pairing
or encryption.

## Embedded-friendly design

The shared code avoids direct calls to `malloc`, `calloc`, `realloc`, and `free`.
Its main buffers and protocol state are owned by caller-provided structures,
which makes the core suitable for embedded systems with predictable memory
budgets. mbedTLS uses its fixed-buffer allocator, configured through
`AIRPLAY_CRYPTO_MEMORY_SIZE`. External components such as FDK-AAC, the platform
SDK, and the board audio driver may still have their own allocation requirements.

Edit [airplay_config.h](airplay_config.h) to configure the speaker name, ports,
audio defaults, and memory limits. The default name is `TestSpeaker`.

## Build

Use C11 and C++ compilers and CMake 3.20 or newer. Run the commands below from this directory. All platforms use CPM.cmake to fetch and build mbedTLS 2.28.8 and FDK-AAC 2.0.3 automatically. To use existing source trees, set `-DCPM_mbedtls_SOURCE=/path/to/mbedtls` and `-DCPM_fdk_aac_SOURCE=/path/to/fdk-aac`.

### Windows

Install MinGW-w64 GCC, CMake, and Ninja, and make them available on `PATH`.

```powershell
cmake -S . -B ../build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build ../build -j 8
ctest --test-dir ../build --output-on-failure
..\build\airplay_player.exe
```

With no arguments, the player selects an active IPv4 adapter. To choose an interface or override the name:

```powershell
..\build\airplay_player.exe 192.168.1.50 020000000050 MySpeaker
```

### Linux

Install ALSA development headers and libraries (`libasound2-dev` on Debian/Ubuntu).

```sh
cmake -S . -B ../build -DAIRPLAY_PLATFORM=linux -DCMAKE_BUILD_TYPE=Debug
cmake --build ../build -j 8
ctest --test-dir ../build --output-on-failure
../build/airplay_player 192.168.1.50 020000000050 MySpeaker
```

### Apple (macOS)

Install the Xcode Command Line Tools and CMake. The backend uses the system AudioToolbox framework.

```sh
cmake -S . -B ../build -DAIRPLAY_PLATFORM=apple -DCMAKE_BUILD_TYPE=Debug
cmake --build ../build -j 8
ctest --test-dir ../build --output-on-failure
../build/airplay_player 192.168.1.50 020000000050 MySpeaker
```

Replace the example IP and MAC with the selected interface's values. The MAC is a 12-digit hexadecimal string without separators; the name is optional. Allow mDNS multicast on UDP 5353 and the configured service ports (TCP 5000 and 6000, UDP 6000-6002, and PTP UDP 319-320 by default). Press Ctrl+C to stop.

### Embedded (FreeRTOS and lwIP)

Build this project as part of the board firmware using its C/C++ cross-compilation toolchain. Provide FreeRTOS, lwIP, and the SDK include directories before adding this project:

```cmake
set(AIRPLAY_PLATFORM embedded CACHE STRING "" FORCE)
set(AIRPLAY_PLATFORM_INCLUDE_DIRS
    "${FREERTOS_INCLUDE_DIRS};${LWIP_INCLUDE_DIRS};${BOARD_INCLUDE_DIRS}"
    CACHE STRING "" FORCE)
add_subdirectory(path/to/airplay)
target_link_libraries(firmware PRIVATE airplay_embedded)
```

Here, `firmware` is the board's executable target and the include variables refer to its SDK directories. CMake builds mbedTLS and FDK-AAC with the firmware's toolchain. Build the firmware with its normal CMake toolchain and build commands.

Shared code does not call `malloc`, `calloc`, `realloc`, or `free`. mbedTLS allocations use a statically reserved buffer whose default size is 64 KiB; override `AIRPLAY_CRYPTO_MEMORY_SIZE` consistently if the board needs a different capacity. Platform libraries, lwIP, FreeRTOS, and FDK-AAC retain their own allocation policies.

Implement the four audio functions declared in [board_audio.h](platform/embedded/board_audio.h), plus `airplay_board_time_us` from [runtime.h](platform/embedded/runtime.h). The clock must return Unix UTC microseconds. Audio writes must copy or consume interleaved 16-bit PCM before returning; queued DMA frames must be included in the output-delay estimate.

FreeRTOS must enable dynamic allocation, mutexes, and event groups. Configure lwIP with `NO_SYS=0`, sockets/netconn, socket select, IPv4/TCP/UDP, IGMP, multicast transmit options, and address reuse. The adapter supports `LWIP_COMPAT_SOCKETS=0`. Provide `sys_now()`. Enable the RSA, AES, SHA-1, Base64, entropy, and random-number facilities used by mbedTLS, and configure its entropy, timing, and networking support for the board. FDK-AAC requires C runtime heap allocation and standard I/O.

After the scheduler, network interface, and clock are ready, call `airplay_platform_start(&config)` from one owner task. That task also calls `airplay_platform_stop()` and waits for cleanup. Use the board's IP and MAC, with `.device_name = AIRPLAY_DEVICE_NAME` for the configured name.

Check memory and task-stack settings in `airplay_config.h` against the board's resources. The default compressed-packet payload storage alone uses about 736 KiB; task stacks, decoder state, networking, and DMA require additional memory. macOS and embedded builds still require validation with their target SDKs and hardware.
