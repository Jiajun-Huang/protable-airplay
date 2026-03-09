# AirPlay Audio Debug Log

## Goal

Fix Windows playback issues for the custom AirPlay receiver:

- no audio
- noisy audio
- intermittent crashes
- stutter/jitter
- slow playback (perceived half-speed)

## Initial Symptoms

- RTSP worked, but playback was unstable.
- Audio output moved from silence/noise to audible but with stutter and slow timing.
- Logs eventually showed clear mismatch:
  - `[audio_stats] in ~= 22528/22880 fps`
  - `[audio_stats] out ~= 43000+ fps`
  - `cfg=44100Hz/2ch`
- Packet-loss log showed severe loss:
  - `[pipeline] Received 1000 packets, lost 801 (80.10%)`

## Root Causes Found

1. RTP receive path throughput was too low under bursty traffic, causing large packet drops.
2. RTSP large `SET_PARAMETER` bodies (album art JPEG) created heavy repeated parsing/logging overhead while body chunks were still arriving.
3. Sequence-loss accounting treated out-of-order/wrap cases as giant loss jumps.
4. Playback pacing originally waited for full chunks, which could produce perceived slowdowns under gaps.
5. Some ALAC decode paths effectively produced mono-sized sample counts in stereo sessions, reducing effective input rate.

## Code Changes

### `shared/rtsp.c`

- Increased RTSP receive safety and robustness for big messages.
- Added safer request-line preview logging.
- Added `Content-Length` pre-check to defer full parse until full body is buffered.
- Limited repeated request preview logging during fragmented body receive.

Effect:

- avoids excessive repeated parse work on large JPEG `SET_PARAMETER` payloads
- lowers control-plane CPU overhead during streaming

### `shared/rtp.c`

- Changed `rtp_receiver_poll()` to drain sockets in loops (`recv` until no more data) for audio/control/timing.
- Reduced debug print frequency for RTP packet previews.

Effect:

- prevents backlog growth in UDP socket queues
- reduces packet drop from processing lag

### `shared/audio_pipeline.c`

- Improved packet-loss counting: only count forward sequence jumps (`delta > 0`), ignore reorder/wrap as loss.
- Added ALAC compatibility expansion for mono-sized decoded output in stereo sessions.

Effect:

- packet-loss stats become meaningful
- avoids half-rate effective input when decoder output is mono-sized for stereo stream

### `shared/sdp.c`

- Added sample-rate sanitization guard:
  - fallback to `44100` if outside sane range (`<8000` or `>192000`).

Effect:

- prevents invalid SDP values from breaking timing setup

### `platform/windows/audio.c`

- Increased `waveOut` buffer depth and size.
- Switched from immediate drop to short wait + free-buffer search.

Effect:

- fewer output underruns and fewer dropped chunks

### `platform/windows/audio_output.c`

- Implemented ring-buffered async playback thread.
- Added startup preroll and max-latency trimming.
- Changed chunk strategy to retry pending chunk on temporary output saturation.
- Added real-time instrumentation:
  - `[audio_stats] in=... out=... ring=... cfg=...`
- Current tuning constants:
  - `AUDIO_RING_SECONDS 3`
  - `AUDIO_CHUNK_FRAMES 352`
  - `AUDIO_PREROLL_MS 180`
  - `AUDIO_MAX_LATENCY_MS 320`

Effect:

- better resilience to jitter and output backpressure
- clear observability for input/output rate mismatch

## Validation Strategy Used

1. Rebuild and run repeatedly with real sender traffic.
2. Verify RTSP handshake stability under repeated `OPTIONS/SET_PARAMETER`.
3. Monitor `audio_stats` and packet-loss logs.
4. Correlate user hearing feedback with metrics (`in`, `out`, `ring`, `lost%`).

## Most Important Diagnostic Findings

- Slow playback was not caused by a zero sample rate.
- It was caused by effective under-feeding and heavy packet loss in the receive path.
- `in` rate around `~22880 fps` with `cfg=44100Hz/2ch` indicated only about half of needed frame feed.

## Audio Pipeline + Crypto Flow

This section documents how encrypted AirPlay audio is processed end-to-end.

1. `audio_pipeline_create()`

- Creates RTP receiver and registers callbacks for audio/control/timing.

2. `audio_pipeline_configure()`

- Copies SDP session parameters (`codec`, `sample_rate`, `channels`, `frames_per_packet`, encryption fields).
- If codec is ALAC, initializes decoder with fmtp values.
- If encryption is present:
  - calls `crypto_rsa_decrypt_aes_key()` to decrypt `rsaaeskey` using the embedded Airport private key.
  - calls `crypto_aes_init()` with decrypted key + SDP `aesiv`.

3. `audio_pipeline_poll()`

- Calls `rtp_receiver_poll()` to drain queued RTP/RTCP/timing packets.

4. `audio_pipeline_on_rtp_audio()`

- Tracks sequence/loss counters.
- Dispatches decode path by codec:
  - ALAC path: `audio_pipeline_decode_alac()`
  - PCM path: `audio_pipeline_decode_pcm()`
- Emits decoded PCM to platform output callback.

5. Decrypt behavior in decode functions

- If encrypted, only the full 16-byte aligned prefix is AES-CBC decrypted.
- Any trailing bytes (not a full AES block) are copied as plaintext.
- The combined buffer is then decoded.

### Crypto Rules That Must Stay Intact

- RSA decrypt uses OAEP + SHA1 (`rsaaeskey` compatibility).
- AES decryption resets CBC IV for each packet (AirPlay behavior in this implementation).
- Do not silently continue playback if RSA AES-key decrypt fails.

### Practical Debug Checks

- If logs show `AES-128-CBC decryption enabled` but audio is noise:
  - verify RSA decrypt success log appears first
  - verify per-packet AES decrypt does not fail in decode path
- If playback is slow:
  - compare `audio_stats in/out`
  - inspect packet loss log in pipeline

## Current Status

- Receiver is significantly more stable and instrumented.
- Remaining quality depends mainly on whether RTP loss remains low in long runs after drain-path fixes.

## Next Checks (if needed)

- Target healthy runtime indicators:
  - `in` close to `44100 fps`
  - `out` close to `44100 fps`
  - `ring` stable (not pinned at 0 or growing unbounded)
  - low `lost%` in pipeline log

- If loss remains high:
  - increase socket receive buffer (`SO_RCVBUF`) in UDP layer
  - reduce remaining hot-path logs during stream
  - add sequence-gap concealment for short losses

## Reference

- AirPlay RTSP notes: https://emanuelecozzi.net/docs/airplay2/rtsp/
