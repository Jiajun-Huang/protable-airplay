# AirPlay Audio Debugging Experience

## Music Playback: Noise After Connecting

The iPhone could discover and connect to the Windows receiver, but music produced noise. After rebuilding and reconnecting to `TestSpeaker`, the user confirmed normal sound.

The working session provided the following evidence:

- ALAC at 44100 Hz, 16-bit stereo, with 352 PCM frames per packet.
- SDP `fmtp`: `352 0 16 40 10 14 2 255 0 0 44100`.
- Successful RSA key unwrapping and AES-128-CBC initialization.
- 704 interleaved samples per normal packet. An initial silence packet had a zero peak; later music packets had nonzero peaks, including 810 and 3390.
- A continuous receive count reached 5000 packets with no loss reported by the application's counter.

These observations establish that the tested playback path worked. They do not identify one change as the sole cause of the original noise: the same failing packet capture was not retained for an isolated before/after comparison. Application counters also do not replace a full network capture when proving packet loss or reordering.

## Reference Implementation Cross-Check

The reference was [rbouteiller/airplay-esp32](https://github.com/rbouteiller/airplay-esp32/tree/9ef96118e5ed149958c7913d9708824f5b97cb13), inspected at commit `9ef96118e5ed149958c7913d9708824f5b97cb13`. The comparison concerned protocol and data-processing behavior; its ESP-IDF audio components were not imported.

| Check | Finding |
| --- | --- |
| SDP to ALAC configuration | The eleven `fmtp` fields map to frame length, bit depth, Rice parameters, channels, and sample rate. The local mapping matched the [reference configuration code](https://github.com/rbouteiller/airplay-esp32/blob/9ef96118e5ed149958c7913d9708824f5b97cb13/main/alac_magic_cookie.c). |
| AES packet boundaries | Reset the session IV for every RTP audio packet, decrypt complete 16-byte blocks, and preserve the trailing partial block. The local crypto implementation already followed this rule; regression coverage was added. See the [reference crypto implementation](https://github.com/rbouteiller/airplay-esp32/blob/9ef96118e5ed149958c7913d9708824f5b97cb13/main/audio/audio_crypto.c). |
| Retransmission framing | Remove the four-byte outer header from type `0x56` before parsing the inner RTP packet. The receiver now also routes retransmissions received on the control port into audio processing. See the [reference stream implementation](https://github.com/rbouteiller/airplay-esp32/blob/9ef96118e5ed149958c7913d9708824f5b97cb13/main/audio/audio_stream_realtime.c). |
| Decoder output length | Interpret output using the negotiated channel count. A decode failure must not produce playable PCM. The local wrapper returns zero samples on failure and does not infer mono output from sample count. See the [reference decoder wrapper](https://github.com/rbouteiller/airplay-esp32/blob/9ef96118e5ed149958c7913d9708824f5b97cb13/main/audio/audio_decoder.c). |
| Service capabilities | Advertise only the transport and encryption paths implemented by the receiver. The local RAOP configuration uses UDP, RSA/AES, and `ek=1`. See the [reference discovery configuration](https://github.com/rbouteiller/airplay-esp32/blob/9ef96118e5ed149958c7913d9708824f5b97cb13/main/network/mdns_airplay.c). |

The reference includes additional AirPlay 2 paths. Match the actual ANNOUNCE/SDP and encryption mode before comparing framing or key handling across implementations.

## Confirmed Decoder and Transport Defects

The decoder previously did not consistently bound bit reads by the supplied packet length. Unsupported element types could leave a preset output length without producing the corresponding PCM. Input bounds, Rice zero-run limits, predictor checks, channel checks, and output-capacity checks now make truncated or unsupported input fail with zero output samples. Failed packets are not submitted to the audio device.

A separate heuristic treated a decoded stereo packet containing `frames_per_packet` samples as mono and duplicated its channels. A short stereo packet can have that sample count, so the count alone does not establish channel layout. Output now follows the session channel count and the decoder's actual result.

For the observed session, the units are:

```text
352 PCM frames x 2 channels = 704 int16_t samples
704 samples x 2 bytes = 1408 PCM bytes
352 / 44100 seconds = approximately 7.98 ms
```

Compressed RTP payload lengths vary; they are not required to equal the decoded PCM byte count. The PCM path rejects incomplete channel frames, and gain is applied once in the shared pipeline.

RTP parsing handles CSRC entries, extensions, padding, and retransmission framing. Audio processing validates the negotiated payload type. Session reconfiguration rebuilds decoder and AES state, and initialization failures propagate as errors. RTSP and audio threads exchange locked session snapshots; the audio thread owns the decoder and output device.

These changes correct identifiable failure paths. There is no separate evidence that retransmission handling was decisive for the original noise, and the successful playback logs did not establish that this branch had to run.

## Validation Methods

During the noise investigation, six CTest cases passed on both Windows with MinGW GCC and Linux under WSL Ubuntu:

| Test | Coverage |
| --- | --- |
| `core_protocol` | IPv4, SDP, RTP extensions/padding/retransmissions, time calculations |
| `network_contract` | Real TCP/UDP loopback, timeouts, EOF, empty datagrams, truncation, send backpressure |
| `service_lifecycle` | Initialization cleanup, service failure, PCM byte order, gain |
| `rtsp_stream` | Fragmented and consecutive requests, bodies, session ownership, disconnects, send failures |
| `mdns_wire` | DNS fields and records, unicast replies, goodbye records, invalid compression pointers |
| `alac_samples` | Independent expected PCM, truncation, unsupported types, channel mismatch, AES packet decryption |

ALAC fixtures were encoded with PyAV/FFmpeg and independently decoded with FFmpeg. They cover synthetic stereo tones, silence, and mono audio. The project decoder matches the expected samples individually. Fixtures are stored in `tests/fixtures/alac.h`; C tests need no Python runtime. Regenerating fixtures requires PyAV.

AES checks encrypt ALAC packets with a fixed key and IV, including a partial-block tail. Repeated decryption checks per-packet IV reset, followed by PCM comparison. This verifies AES packet handling, not RSA negotiation; the RSA evidence came from the real iPhone session.

Real-device playback and user listening confirmed the final sound. Automated tests, session logs, and listening assess different parts of the result and cannot substitute for one another.

## Reusable Investigation Procedure

1. Record the exact executable, sender, receiver, and symptom. Discovery and connection alone establish only part of the control path.
2. Find the first incorrect boundary: SDP parameters, RTP type and length, encryption initialization, decoded sample count and peak, then PCM output format. An initial silence packet is not itself an error.
3. Check units and ownership. Distinguish compressed bytes, PCM frames, channel samples, and output bytes. Async audio APIs and DMA must not retain a decoder buffer after it can be reused.
4. Verify algorithms against independent expected output. A success return code does not prove correct samples; malformed input must fail explicitly.
5. Compare the active protocol path with the reference implementation, including framing, encryption boundaries, and state changes.
6. Separate evidence of a working repair from evidence of a unique root cause. Proving one cause requires a controlled comparison using the same failing input.

Embedded memory, stack usage, DMA consumption, and scheduling still require board validation. Linux speaker output and the macOS backend have not been validated on their target audio hardware.

## YouTube: Audio Ahead of Video

The same iPhone-to-Windows setup later produced a roughly fixed audio lead during YouTube playback. The pipeline decoded and submitted packets on arrival. It did not process the control packet's clock anchor, preserve the sender's SETUP timing port, or complete timing request/reply exchanges. Correct PCM therefore reached the device at the wrong time.

The current timing path:

- Reads the sender's `timing_port` from SETUP and exchanges RAOP `0x52/0x53` packets. The three timestamp fields are at byte offsets 8, 16, and 24 of a 32-byte timing packet. Round-trip delay and sender/local timestamps determine clock offset; replies must match the outstanding request.
- Parses `0x54` synchronization packets. The RTP timestamp at offset 4 identifies the frame being played at the network time stored at offset 8. The RTP timestamp at offset 16 identifies the sending position; their difference represents advance delivery. See the [reference synchronization handling](https://github.com/rbouteiller/airplay-esp32/blob/9ef96118e5ed149958c7913d9708824f5b97cb13/main/audio/audio_stream_realtime.c).
- Buffers, orders, and deduplicates compressed packets, then schedules them against the mapped local time while accounting for queued output frames.
- Applies RECORD/FLUSH `RTP-Info` boundaries and handles sequence-number and RTP-timestamp wraparound.

The observed iPhone session reported an advance of 77175 frames, or 1750 ms. The running queue held about 220 packets and clock exchanges succeeded. The user confirmed normal YouTube synchronization. The 1.75-second value was measured in that session, not applied as a fixed delay for every app.

The `scheduled_audio` test covers positive and negative clock offsets, NTP/RTP wraparound, reordering, duplicates, full queues, FLUSH boundaries, early/on-time/late output, and device queue delay. RTSP tests also validate timing-port and timestamp parameters. All seven tests passed on Windows and Linux.

Active retransmission requests, audio-clock drift resampling, and multi-device synchronization are not implemented. FLUSH filters whole packets rather than retaining a partial packet across its boundary. If sender clock synchronization is unavailable, playback uses a two-second arrival-based buffer and logs the condition; this mode does not guarantee video synchronization.

## Bilibili: Verify That the Input Contains Sound

The user selected `TestSpeaker` through iPhone Control Center. Connection, ALAC negotiation, RSA/AES setup, clock synchronization, and packet reception worked, but decoded nonzero-sample counts remained zero.

To check whether the project decoder was incorrectly producing silence, a short local capture recorded decrypted ALAC payloads and project output. FFmpeg/PyAV independently decoded the same payloads for a sample-by-sample comparison:

| Measurement | Result |
| --- | ---: |
| Packets | 5000 |
| Duration | Approximately 39.9 seconds |
| Compressed payload per packet | 32 bytes |
| Packets with decoder disagreement | 0 |
| Packets containing nonzero PCM | 0 |
| Absolute PCM peak | 0 |

The captured input was a valid, decodable silence stream. Receiver volume, PCM byte order, or additional delay cannot recover sound absent from that stream. This result alone does not explain why the sender produced silence.

Discovery advertises the implemented `_raop._tcp` audio service. Removing the additional `_airplay._tcp` advertisement did not resolve the Bilibili symptom. The user confirmed that the same video produced sound when output returned to the iPhone. The unresolved behavior is therefore specific to the Bilibili/iOS output path to this receiver; its cause remains unconfirmed.

Diagnostic capture files stay in the build directory and are not part of the receiver build. A real 32-byte silence packet is retained as an ALAC regression fixture, with an expected result of 704 zero samples.

The Safari comparison could not be completed because the webpage either would not play or redirected to the app. There is insufficient evidence to identify a definite app defect or claim receiver compatibility. Further attribution requires sender-side information or a controlled comparison using the same phone and video with another receiver. This unresolved silence issue is separate from the confirmed YouTube timing repair.
