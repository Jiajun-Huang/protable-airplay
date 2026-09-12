# AirPlay 2 Gap Analysis

## Scope

This document compares the current receiver with the AirPlay 2 architecture used by `rbouteiller/airplay-esp32`. It describes the missing protocol pieces, the impact of each gap, and the work required before advertising the receiver as a reliable AirPlay 2 device.

Reference project: <https://github.com/rbouteiller/airplay-esp32>

## Executive Summary

The current project is a traditional RAOP receiver with experimental AirPlay 2 code. It can advertise `_airplay._tcp`, parse selected AirPlay 2 binary plist messages, perform partial transient pairing, derive or accept some audio keys, and process PTP packets. These pieces are not yet a complete interoperable AirPlay 2 implementation.

The main connection failure is that discovery advertises AirPlay 2 before the device has a complete HAP identity and authentication flow. An iPhone can discover `TestSpeaker`, but it can fail during `pair-verify`, RTSP channel encryption, or AirPlay 2 stream setup.

## Capability Matrix

| Area | Current project | AirPlay 2 requirement | Status |
| --- | --- | --- | --- |
| Traditional RAOP | `_raop._tcp`, RTSP, UDP RTP | Required for AirPlay 1 compatibility | Working path |
| ALAC over UDP | ALAC decoder and RTP pipeline | Realtime AirPlay 1 stream | Implemented |
| AAC decoding | FDK-AAC and MPEG4-GENERIC parsing | AAC buffered AirPlay 2 stream | Partial; transport path is incomplete |
| `_airplay._tcp` | Advertised | AirPlay 2 discovery service | Advertised but incomplete |
| Device identity | MAC-based identity only | Stable Ed25519 identity | Missing |
| `pk` TXT field | Missing | Ed25519 public key | Missing |
| `pi` TXT field | Missing or placeholder | Pairing identity UUID | Missing |
| `/info` | Partial binary plist response | AirPlay 2 capability and identity response | Partial |
| Pair-setup | Simplified SRP-like transient flow | HAP pair-setup M1-M6 | Partial and incomplete |
| Pair-verify | Missing | HAP pair-verify M1-M4 | Missing |
| RTSP encryption | Custom transient record encryption | HAP ChaCha20-Poly1305 channel encryption | Partial; not interoperable yet |
| Audio key derivation | Partial `shk`/`ekey` handling | HAP-derived AirPlay audio keys | Partial |
| AirPlay 2 audio | Binary plist setup exists | Type 96 realtime and type 103 buffered streams | Partial |
| Buffered AAC | Decoder exists | TCP stream, jitter buffer, framing and backpressure | Missing/incomplete |
| PTP | Packet parser and deadline calculation | Clock synchronization and stream timing | Partial |
| Event port | Basic socket support | Persistent AirPlay 2 event channel | Partial |
| Persistence | No stable HAP identity store | Persistent Ed25519 keypair | Missing |
| Multi-room | No complete group behavior | PTP peer/master handling | Missing |

## 1. Discovery and Identity

### Current implementation

The project currently registers both `_raop._tcp` and `_airplay._tcp` from [shared/airplay/airplay_discovery.c](../shared/airplay/airplay_discovery.c). The AirPlay 2 TXT data includes fields such as `deviceid`, `features`, `flags`, `model`, `srcvers`, `vv`, and `acl`.

The AirPlay 2 service is currently tied to the existing RTSP port configuration. The device identity is derived from the network MAC address.

### Missing compared with `airplay-esp32`

The reference project generates or loads a long-term Ed25519 keypair and publishes the public key as:

```text
pk=<64 hexadecimal characters>
```

It also publishes a stable pairing identity:

```text
pi=<UUID>
```

The same Ed25519 public key is returned by `/info` as binary plist data. The key remains stable across reboot and reconnect.

### Impact

Advertising AirPlay 2 without `pk`, `pi`, and a stable identity can allow discovery but prevents the sender from completing the expected authentication flow.

### Required work

1. Add a platform-neutral identity store interface.
2. Implement file-backed storage for Windows/Linux and a board storage adapter for embedded targets.
3. Generate an Ed25519 keypair only when no valid keypair exists.
4. Add `pk` and `pi` to `_airplay._tcp` and the compatible `_raop._tcp` TXT records.
5. Return the same public key from `/info`.
6. Do not advertise AirPlay 2 until identity initialization succeeds.

## 2. HAP Pair-Setup

### Current implementation

The current [shared/airplay/pairing.c](../shared/airplay/pairing.c) implements a limited transient SRP-style exchange and can derive control keys for the local test path. It does not implement the complete HAP pairing record exchange.

### Required AirPlay 2 flow

The reference project separates the HAP implementation into TLV8, SRP, pairing, and crypto modules. Pair-setup consists of:

```text
M1: client method, state, flags
M2: server state, salt, SRP public key
M3: client SRP public key and proof
M4: server proof
M5: encrypted client identity and signature
M6: encrypted server identity and signature
```

The server must validate the client proof, decrypt M5, verify the client Ed25519 signature, and return an authenticated M6 response containing the server identity and signature.

### Missing

- Complete TLV8 fragmentation and validation rules.
- Pair-setup M5 and M6.
- Ed25519 client identity verification.
- Server Ed25519 signature generation.
- Persistent pairing records, if non-transient pairing is supported.
- Correct distinction between transient AirPlay pairing and long-term HomeKit pairing.

### Impact

A client may receive a superficially valid early response but will reject the session when it expects the encrypted identity exchange.

## 3. HAP Pair-Verify

### Current implementation

There is no complete `POST /pair-verify` state machine. The current RTSP dispatcher primarily handles `POST /pair-setup` and then enables a custom encrypted record mode.

### Reference flow

The reference project creates a per-connection HAP session containing:

```text
long-term Ed25519 device keypair
ephemeral X25519 keypair
client ephemeral public key
X25519 shared secret
read and write session keys
read and write counters
```

Pair-verify is conceptually:

```text
M1: client ephemeral X25519 public key
M2: server ephemeral public key + Ed25519 signature
M3: client encrypted identity + signature
M4: encrypted server confirmation
```

The shared X25519 secret is expanded with the protocol-defined HKDF labels to create the RTSP read/write keys.

### Missing

- X25519 ephemeral key generation.
- Ed25519 signature verification and signing.
- Pair-verify M1-M4.
- Per-client session state and failure cleanup.
- Correct HKDF labels and direction-specific key assignment.
- Replay, counter, and nonce handling tied to the verified session.

### Impact

Without pair-verify, the iPhone cannot establish the authenticated AirPlay 2 RTSP channel. This is the primary reason a discovered device can still show `Unable to Connect`.

## 4. Encrypted RTSP Channel

### Current implementation

The project has ChaCha20-Poly1305 primitives and a custom record wrapper. RTSP clients track encrypted state and counters in [shared/rtsp.h](../shared/rtsp.h) and [shared/rtsp.c](../shared/rtsp.c).

### Reference behavior

The reference implementation frames encrypted RTSP data as:

```text
2-byte little-endian plaintext length
ChaCha20-Poly1305 ciphertext
16-byte authentication tag
```

The two-byte length is authenticated as AAD. Read and write nonces use independent monotonically increasing counters. Partial socket reads must be accumulated until a complete encrypted frame is available.

### Missing or needing verification

- Pair-verify-derived keys instead of the current simplified pairing keys.
- Exact protocol labels and key direction.
- Complete encrypted request and response transition at the pair-verify boundary.
- Correct handling of partial reads and multiple encrypted frames.
- Counter reset and teardown behavior per connection.
- Interoperability tests using captured M1-M4 frames.

## 5. AirPlay 2 RTSP and Binary Plists

### Current implementation

The project has [shared/airplay/airplay2.c](../shared/airplay/airplay2.c) with partial support for:

- `GET /info`
- `POST /pair-setup`
- binary plist `SETUP`
- `SETRATEANCHORTIME`
- `FLUSHBUFFERED`
- `SETPEERS`
- `/feedback`, `/audioMode`, and `/command`

### Missing or incomplete

- `POST /pair-verify` dispatch and state transitions.
- Full AirPlay 2 `/info` response fields, including `pk` and stable identity data.
- Complete event-port lifecycle and reconnect behavior.
- Exact initial SETUP and stream SETUP behavior for all sender variants.
- Correct handling of encrypted and unencrypted request boundaries.
- Complete response fields for stream type, audio formats, latency, and ports.
- Robust support for both AirPlay 1 RTSP and AirPlay 2 RTSP on separate connection modes.

## 6. Audio Transport

### AirPlay 1 path

The current project has a working direction for traditional RAOP:

```text
UDP RTP -> ALAC/PCM decode -> playout -> platform PCM output
```

### AirPlay 2 path in the reference project

The reference separates two stream types:

```text
Type 96: realtime ALAC over UDP
Type 103: buffered AAC over TCP
```

The buffered stream has a deep jitter buffer and handles TCP framing, backpressure, retransmission/continuity, decryption, and delayed decoding.

### Current gaps

- No complete buffered TCP audio receiver.
- No TCP packet framing and stream lifecycle matching AirPlay 2.
- No deep AAC jitter buffer separate from the realtime UDP queue.
- No backpressure strategy for a sender that transmits ahead.
- No full stream switching between type 96 and type 103.
- Audio key setup and decryption are not validated against real AirPlay 2 captures.

FDK-AAC is already linked and the project has an AAC decoder wrapper, but that alone does not implement the AirPlay 2 buffered transport.

## 7. PTP Timing

### Current implementation

The project has [shared/ptp_sync.c](../shared/ptp_sync.c), PTP packet parsing, clock ID filtering, anchor handling, and deadline calculation. A regression test covers timestamp wraparound and stale-sample handling.

### Missing compared with the reference

- Full PTP master selection and peer management.
- SETPEERS-driven clock topology updates.
- Continuous clock drift estimation.
- Clock servo behavior rather than a single offset sample.
- Integration of PTP timing with buffered TCP playout.
- Pause/resume and seek anchor continuity.
- Multi-room synchronization validation.

The current PTP code should be treated as a protocol and timing foundation, not complete AirPlay 2 synchronization.

## 8. Persistent Pairing and Platform Storage

The reference uses ESP-IDF NVS to persist the Ed25519 public and secret keys under the `airplay` namespace. The current cross-platform project has no equivalent secure storage contract.

A portable design should define:

```c
int identity_load(uint8_t public_key[32], uint8_t secret_key[64]);
int identity_store(const uint8_t public_key[32], const uint8_t secret_key[64]);
```

Platform implementations should protect the secret key with restrictive permissions or platform key storage. The secret key must not be regenerated on every process start, otherwise the advertised `pk` and the device identity used during verification will disagree.

## 9. Why Discovery Currently Fails

The current receiver advertises AirPlay 2 capabilities before all required protocol stages are complete:

```text
_airplay._tcp is visible
features and vv=2 are advertised
pair-setup is partial
pair-verify is missing
pk/pi identity is incomplete
AirPlay 2 RTSP encryption is incomplete
```

The sender can therefore discover `TestSpeaker` but fail during authorization. This is expected until the identity, pair-verify, and encrypted RTSP path are complete.

For a stable traditional RAOP build, either:

1. disable `_airplay._tcp` and advertise only the implemented RAOP capabilities; or
2. keep AirPlay 2 discovery disabled behind a build option until the complete authentication path passes interoperability tests.

## 10. Recommended Implementation Order

1. Add portable persistent Ed25519 identity storage.
2. Add Ed25519 and X25519 backend selection for desktop and embedded targets.
3. Implement and test TLV8 fragmentation and validation.
4. Replace the simplified pair-setup with complete HAP M1-M6.
5. Implement pair-verify M1-M4 and derive direction-specific RTSP keys.
6. Replace the custom encrypted RTSP transition with the verified HAP channel.
7. Correct mDNS TXT and `/info` identity fields.
8. Implement the AirPlay 2 buffered TCP AAC stream and jitter buffer.
9. Connect PTP servo and anchors to both realtime and buffered playout.
10. Add captured-protocol tests, malformed-input tests, and real iPhone interoperability testing.
11. Enable `_airplay._tcp` by default only after these tests pass.

## Validation Checklist

- [ ] Device keeps the same Ed25519 public key across restart.
- [ ] `_airplay._tcp` TXT includes valid `pk`, `pi`, `features`, `flags`, and `vv`.
- [ ] `GET /info` returns the same public key as mDNS.
- [ ] Pair-setup M1-M6 passes against a known HAP client.
- [ ] Pair-verify M1-M4 passes and rejects modified signatures.
- [ ] Encrypted RTSP request and response counters remain synchronized.
- [ ] Type 96 ALAC realtime stream plays with PTP anchor timing.
- [ ] Type 103 AAC buffered stream plays over TCP.
- [ ] 44.1 kHz and 48 kHz streams have explicit output/resampling behavior.
- [ ] Pause, resume, seek, FLUSHBUFFERED, and TEARDOWN recover cleanly.
- [ ] Traditional RAOP remains compatible when AirPlay 2 is disabled.

## Reference

- `rbouteiller/airplay-esp32` HAP implementation: <https://github.com/rbouteiller/airplay-esp32/tree/main/main/hap>
- `rbouteiller/airplay-esp32` RTSP implementation: <https://github.com/rbouteiller/airplay-esp32/tree/main/main/rtsp>
- `rbouteiller/airplay-esp32` audio implementation: <https://github.com/rbouteiller/airplay-esp32/tree/main/main/audio>
- `rbouteiller/airplay-esp32` architecture: <https://rbouteiller.github.io/airplay-esp32/reference/architecture/>
