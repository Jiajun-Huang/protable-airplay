#ifndef AIRPLAY_CONFIG_H
#define AIRPLAY_CONFIG_H

/* Central compile-time configuration for receiver identity, capabilities,
 * network ports, audio
 * defaults, memory limits, and embedded task sizing.
 * Rebuild the target after changing any value
 * in this file. */
#define AIRPLAY_LOG_LEVEL AIRPLAY_LOG_DEBUG

/* Device identity. The CLI NAME argument or airplay_config_t.device_name can override the name. */
#define AIRPLAY_DEVICE_NAME    "TestSpeaker"
#define AIRPLAY_MODEL_NAME     "PortableSpeaker"
#define AIRPLAY_SERVER_VERSION "130.14"

/* Shared by socket listeners, mDNS SRV records, and RTSP SETUP replies. UDP ports must differ. */
#define AIRPLAY_RTSP_PORT        5000
#define AIRPLAY_AUDIO_PORT       6000
#define AIRPLAY_CONTROL_PORT     6001
#define AIRPLAY_TIMING_PORT      6002
#define AIRPLAY_BUFFERED_PORT    6000 /* TCP; realtime audio uses UDP on this port. */
#define AIRPLAY_PTP_EVENT_PORT   319
#define AIRPLAY_PTP_GENERAL_PORT 320
#define AIRPLAY2_SOURCE_VERSION  "377.40.00"
/* Audio, buffered audio, PTP and transient pairing; no persistent HomeKit pairing. */
#define AIRPLAY2_FEATURES_LOW            0x405C4A00
#define AIRPLAY2_FEATURES_HIGH           0x18340
#define AIRPLAY2_FEATURES_TEXT           "0x405C4A00,0x18340"
#define AIRPLAY2_REALTIME_LATENCY_FRAMES 11025

/* Audio defaults. Playback uses the sender's SDP parameters; the PCM output interface is 16-bit. */
#define AIRPLAY_DEFAULT_SAMPLE_RATE       44100
#define AIRPLAY_DEFAULT_CHANNELS          2
#define AIRPLAY_DEFAULT_BITS_PER_SAMPLE   16
#define AIRPLAY_DEFAULT_FRAMES_PER_PACKET 352
#define AIRPLAY_DEFAULT_VOLUME_DB         (-20.0f)
/* Receiver latency advertised in RECORD replies, in PCM frames. Playback follows the sender clock.
 */
#define AIRPLAY_AUDIO_LATENCY_FRAMES 2205

#define AIRPLAY_STRINGIFY_VALUE(value) #value
#define AIRPLAY_STRINGIFY(value)       AIRPLAY_STRINGIFY_VALUE(value)

/* RAOP TXT capabilities. deviceid is generated from the network interface MAC address.
 * cn=0,1: PCM/ALAC; et=0,1: unencrypted/RSA-AES; tp=UDP.
 * Advertised capabilities must match the implemented protocols.
 * Stringified numeric settings must be decimal literals without parentheses or U/L suffixes. */
#define AIRPLAY_RAOP_TXT_ENTRIES                                                                   \
    "txtvers=1", "ch=" AIRPLAY_STRINGIFY(AIRPLAY_DEFAULT_CHANNELS), "cn=0,1,2", "da=true",         \
        "et=0,1", "md=0,1,2", "pw=false", "sv=false", "ft=" AIRPLAY2_FEATURES_TEXT, "vv=2",        \
        "sr=" AIRPLAY_STRINGIFY(AIRPLAY_DEFAULT_SAMPLE_RATE),                                      \
        "ss=" AIRPLAY_STRINGIFY(AIRPLAY_DEFAULT_BITS_PER_SAMPLE), "tp=UDP", "vn=65537",            \
        "vs=" AIRPLAY_SERVER_VERSION, "am=" AIRPLAY_MODEL_NAME, "sf=0x4", "ek=1"

/* Memory limits. CMake defines AIRPLAY_TARGET_EMBEDDED for embedded builds.
 * Board projects may override these limits with consistent definitions in every translation unit.
 */
#ifndef RTSP_RX_BUFFER_SIZE
#ifdef AIRPLAY_TARGET_EMBEDDED
#define RTSP_RX_BUFFER_SIZE (16 * 1024) /* Bytes per RTSP client */
#else
#define RTSP_RX_BUFFER_SIZE (256 * 1024)
#endif
#endif
#ifndef RTSP_MAX_CLIENTS
#define RTSP_MAX_CLIENTS 2
#endif
#ifndef SDP_SCRATCH_MAX
#define SDP_SCRATCH_MAX 4096 /* SDP parser scratch bytes */
#endif
#ifndef RTP_BUFFER_SIZE
#define RTP_BUFFER_SIZE 4096 /* Maximum bytes per RTP datagram */
#endif
#ifndef MAX_AUDIO_BUFFER_SAMPLES
#define MAX_AUDIO_BUFFER_SAMPLES (RTP_BUFFER_SIZE / 2) /* int16_t samples across all channels */
#endif
#ifndef ALAC_MAX_SAMPLES_PER_FRAME
#define ALAC_MAX_SAMPLES_PER_FRAME 352 /* Maximum PCM frames per packet */
#endif
#ifndef ALAC_MAX_CONTEXTS
#define ALAC_MAX_CONTEXTS 1
#endif
#ifndef AIRPLAY_CRYPTO_MEMORY_SIZE
#define AIRPLAY_CRYPTO_MEMORY_SIZE (64 * 1024) /* Fixed mbedTLS allocation pool. */
#endif
#ifndef AIRPLAY_PLAYOUT_PACKETS
#define AIRPLAY_PLAYOUT_PACKETS 512 /* Compressed packets; must hold the sender's advance audio */
#endif

/* Desktop output buffers. Windows/macOS queue blocks; Linux uses an ALSA latency target. */
#define AIRPLAY_OUTPUT_BUFFER_COUNT   8
#define AIRPLAY_OUTPUT_BUFFER_SAMPLES 16384 /* int16_t samples per block across all channels */
#define AIRPLAY_ALSA_BUFFER_US        100000

/* FreeRTOS stack depths in StackType_t units; priority is relative to the idle task. */
#ifndef AIRPLAY_MDNS_STACK_WORDS
#define AIRPLAY_MDNS_STACK_WORDS 2048
#endif
#ifndef AIRPLAY_RTSP_STACK_WORDS
#define AIRPLAY_RTSP_STACK_WORDS 8192
#endif
#ifndef AIRPLAY_AUDIO_STACK_WORDS
#define AIRPLAY_AUDIO_STACK_WORDS 4096
#endif
#define AIRPLAY_TASK_PRIORITY_OFFSET 2

#if AIRPLAY_RTSP_PORT < 1 || AIRPLAY_RTSP_PORT > 65535 || AIRPLAY_AUDIO_PORT < 1 ||                \
    AIRPLAY_AUDIO_PORT > 65535 || AIRPLAY_CONTROL_PORT < 1 || AIRPLAY_CONTROL_PORT > 65535 ||      \
    AIRPLAY_TIMING_PORT < 1 || AIRPLAY_TIMING_PORT > 65535
#error "AirPlay service ports must be in 1..65535"
#endif
#if AIRPLAY_AUDIO_PORT == AIRPLAY_CONTROL_PORT || AIRPLAY_AUDIO_PORT == AIRPLAY_TIMING_PORT ||     \
    AIRPLAY_CONTROL_PORT == AIRPLAY_TIMING_PORT || AIRPLAY_AUDIO_PORT == 5353 ||                   \
    AIRPLAY_CONTROL_PORT == 5353 || AIRPLAY_TIMING_PORT == 5353
#error "AirPlay UDP ports must be distinct and must not use the mDNS port 5353"
#endif
#if AIRPLAY_DEFAULT_BITS_PER_SAMPLE != 16 || AIRPLAY_DEFAULT_CHANNELS < 1 ||                       \
    AIRPLAY_DEFAULT_CHANNELS > 2
#error "The audio pipeline supports 16-bit mono/stereo PCM"
#endif

#endif
