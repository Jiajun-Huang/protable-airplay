#include "airplay2.h"
#include "fairplay.h"
#include "bplist.h"
#include "playout.h"
#include "log.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <mbedtls/chachapoly.h>

static int status(rtsp_client_t *c, const rtsp_request_t *r, int code, const char *text)
{
    if (code >= 400)
        LOG_WARN("airplay2", "%s: %d %s\n", r->uri, code, text);
    return rtsp_send_response(c, code, text, r->cseq, NULL, NULL, 0);
}
static int plist_response(rtsp_client_t *c, const rtsp_request_t *r,
                           bplist_writer_t *w, uint32_t root)
{
    size_t size = bplist_finish(w, root);
    if (!size) return status(c, r, 500, "Internal Server Error");
    return rtsp_send_response(c, 200, "OK", r->cseq,
                              "Content-Type: application/x-apple-binary-plist\r\n",
                              w->data, size);
}
static void add_int(bplist_writer_t *w, uint32_t *refs, size_t *n, const char *key, uint64_t value)
{
    refs[(*n)++] = bplist_add_string(w, key); refs[(*n)++] = bplist_add_uint(w, value);
}
static void add_text(bplist_writer_t *w, uint32_t *refs, size_t *n, const char *key, const char *value)
{
    refs[(*n)++] = bplist_add_string(w, key); refs[(*n)++] = bplist_add_string(w, value);
}
static uint64_t number(const bplist_t *p, uint32_t dict, const char *key, uint64_t fallback)
{
    uint64_t value;
    return bplist_uint(p, bplist_get(p, dict, key), &value) ? fallback : value;
}
static int info(rtsp_instance_t *s, rtsp_client_t *c, const rtsp_request_t *r)
{
    uint8_t output[2048]; bplist_writer_t w; uint32_t refs[32]; size_t n = 0;
    char device[18];
    snprintf(device, sizeof(device), "%.2s:%.2s:%.2s:%.2s:%.2s:%.2s",
             s->local_mac_hex, s->local_mac_hex + 2, s->local_mac_hex + 4,
             s->local_mac_hex + 6, s->local_mac_hex + 8, s->local_mac_hex + 10);
    bplist_writer_init(&w, output, sizeof(output));
    add_text(&w, refs, &n, "deviceid", device);
    add_text(&w, refs, &n, "name", s->device_name[0] ? s->device_name : AIRPLAY_DEVICE_NAME);
    add_text(&w, refs, &n, "model", AIRPLAY_MODEL_NAME);
    add_text(&w, refs, &n, "protovers", "1.1");
    add_text(&w, refs, &n, "srcvers", AIRPLAY2_SOURCE_VERSION);
    add_int(&w, refs, &n, "vv", 2); add_int(&w, refs, &n, "statusFlags", 4);
    add_int(&w, refs, &n, "features", (uint64_t)AIRPLAY2_FEATURES_HIGH << 32 | AIRPLAY2_FEATURES_LOW);
    uint32_t formats[2];
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t values[6]; size_t count = 0;
        add_int(&w, values, &count, "type", i ? 103 : 96);
        add_int(&w, values, &count, "audioInputFormats", 0x01000000);
        add_int(&w, values, &count, "audioOutputFormats", 0x01000000);
        formats[i] = bplist_add_dict(&w, values, count / 2);
    }
    refs[n++] = bplist_add_string(&w, "audioFormats");
    refs[n++] = bplist_add_array(&w, formats, 2);
    return plist_response(c, r, &w, bplist_add_dict(&w, refs, n / 2));
}
static int stream_key(const bplist_t *p, uint32_t stream, pairing_t *pair, uint8_t key[32])
{
    const uint8_t *data; size_t size;
    if (!bplist_bytes(p, bplist_get(p, stream, "shk"), &data, &size)) {
        if (size != 32) return -1;
        memcpy(key, data, 32); return 0;
    }
    if (bplist_bytes(p, bplist_get(p, stream, "ekey"), &data, &size) || size != 48)
        return -1;
    uint8_t nonce[12] = {0}; mbedtls_chachapoly_context ctx;
    mbedtls_chachapoly_init(&ctx);
    int result = mbedtls_chachapoly_setkey(&ctx, pair->shared_secret);
    if (!result) result = mbedtls_chachapoly_auth_decrypt(&ctx, 32, nonce, NULL, 0,
                                                        data + 32, data, key);
    mbedtls_chachapoly_free(&ctx);
    return result;
}
static int setup(rtsp_instance_t *s, rtsp_client_t *c, const rtsp_request_t *r, const bplist_t *p)
{
    uint8_t output[512]; bplist_writer_t w; uint32_t refs[12]; size_t n = 0;
    bplist_writer_init(&w, output, sizeof(output));
    uint32_t streams = bplist_get(p, p->root, "streams");
    if (streams == BPLIST_NONE) {
        const uint8_t *timing; size_t size;
        if (!bplist_bytes(p, bplist_get(p, p->root, "timingProtocol"), &timing, &size))
            LOG_DEBUG("airplay2", "Requested timing protocol: %.*s\n", (int)size, timing);
        if (bplist_bytes(p, bplist_get(p, p->root, "timingProtocol"), &timing, &size) ||
            size != 3 || memcmp(timing, "PTP", 3))
            return status(c, r, 461, "Unsupported Transport");
        if (c->event_listener.handle == UINTPTR_MAX &&
            net_tcp_listen(&c->event_listener, s->local_ip, 0))
            return status(c, r, 500, "Internal Server Error");
        add_int(&w, refs, &n, "eventPort", c->event_listener.port);
        add_int(&w, refs, &n, "timingPort", 0);
        LOG_INFO("airplay2", "Initial SETUP: PTP, event port %u\n", c->event_listener.port);
        return plist_response(c, r, &w, bplist_add_dict(&w, refs, n / 2));
    }
    if (bplist_count(p, streams) != 1) return status(c, r, 400, "Bad Request");
    uint32_t stream = bplist_at(p, streams, 0);
    uint64_t type = number(p, stream, "type", 0);
    uint64_t codec = number(p, stream, "ct", 0), rate = number(p, stream, "sr", 44100);
    uint64_t frames = number(p, stream, "spf", codec == 2 ? 352 : 1024);
    LOG_DEBUG("airplay2", "Requested stream type=%llu codec=%llu rate=%llu frames=%llu\n",
              (unsigned long long)type, (unsigned long long)codec,
              (unsigned long long)rate, (unsigned long long)frames);
    if ((type != 96 && type != 103) || (codec != 2 && codec != 4) ||
        (rate != 44100 && rate != 48000) ||
        (codec == 2 ? (!frames || frames > ALAC_MAX_SAMPLES_PER_FRAME) : frames != 1024))
        return status(c, r, 415, "Unsupported Media Type");
    sdp_session_t session = {0};
    session.stream_type = (uint16_t)type;
    session.codec = codec == 2 ? SDP_CODEC_ALAC : SDP_CODEC_AAC;
    session.sample_rate = (uint32_t)rate; session.channels = 2; session.bits_per_sample = 16;
    session.frames_per_packet = (uint32_t)frames; session.payload_type = 96;
    uint64_t latency = number(p, stream, "latencyMin", AIRPLAY2_REALTIME_LATENCY_FRAMES);
    if (latency > rate * 5 || stream_key(p, stream, &c->pairing, session.audio_key))
        return status(c, r, 400, "Bad Request");
    session.latency_frames = type == 96 ? (uint32_t)latency : 0;
    if (codec == 4) {
        /* AAC-LC AudioSpecificConfig: object type 2, rate index, stereo. */
        unsigned index = rate == 44100 ? 4 : 3;
        session.aac_config[0] = (uint8_t)(0x10 | (index >> 1));
        session.aac_config[1] = (uint8_t)((index & 1) << 7 | 0x10);
        session.aac_config_len = 2;
    }
    os_mutex_lock(&s->state_lock);
    s->stream.session = session;
    s->stream.has_session = s->stream.recording = 1;
    s->stream.has_timestamp_floor = 0;
    s->stream.has_buffered_flush_sequence = 0;
    s->stream.timing_peer = c->peer; s->stream.timing_peer.port = 0;
    memset(&s->stream.anchor, 0, sizeof(s->stream.anchor));
    ++s->stream.generation; s->stream_owner = c;
    os_mutex_unlock(&s->state_lock);
    add_int(&w, refs, &n, "type", type);
    add_int(&w, refs, &n, "dataPort", type == 103 ? AIRPLAY_BUFFERED_PORT : AIRPLAY_AUDIO_PORT);
    add_int(&w, refs, &n, "controlPort", AIRPLAY_CONTROL_PORT);
    if (type == 103) add_int(&w, refs, &n, "audioBufferSize", AIRPLAY_PLAYOUT_PACKETS * PLAYOUT_PAYLOAD_BYTES);
    uint32_t dict = bplist_add_dict(&w, refs, n / 2);
    uint32_t array = bplist_add_array(&w, &dict, 1);
    uint32_t root[] = {bplist_add_string(&w, "streams"), array};
    LOG_INFO("airplay2", "Stream SETUP: type=%llu codec=%llu rate=%llu frames=%llu\n",
             (unsigned long long)type, (unsigned long long)codec,
             (unsigned long long)rate, (unsigned long long)frames);
    return plist_response(c, r, &w, bplist_add_dict(&w, root, 1));
}
static int set_anchor(rtsp_instance_t *s, rtsp_client_t *c, const rtsp_request_t *r, const bplist_t *p)
{
    double rate;
    if (bplist_real(p, bplist_get(p, p->root, "rate"), &rate) || (rate != 0 && rate != 1))
        return status(c, r, 400, "Bad Request");
    airplay_anchor_t a = {0}; a.playing = rate == 1;
    uint64_t sec, frac, rtp;
    if (a.playing) {
        if (bplist_uint(p, bplist_get(p, p->root, "networkTimeSecs"), &sec) ||
            bplist_uint(p, bplist_get(p, p->root, "networkTimeFrac"), &frac) ||
            bplist_uint(p, bplist_get(p, p->root, "rtpTime"), &rtp) || rtp > UINT32_MAX ||
            bplist_uint(p, bplist_get(p, p->root, "networkTimeTimelineID"), &a.clock_id) ||
            sec > INT64_MAX / 1000000)
            return status(c, r, 400, "Bad Request");
        a.network_us = sec * 1000000 + ((frac >> 32) * 1000000 >> 32);
        a.rtp_time = (uint32_t)rtp; a.valid = 1;
    }
    os_mutex_lock(&s->state_lock);
    if (s->stream_owner != c) { os_mutex_unlock(&s->state_lock); return status(c, r, 455, "Invalid State"); }
    if (a.playing) s->stream.anchor = a;
    else s->stream.anchor.playing = 0;
    os_mutex_unlock(&s->state_lock);
    LOG_INFO("airplay2", "Playback rate=%g RTP=%u clock=%016llx\n", rate, a.rtp_time,
             (unsigned long long)a.clock_id);
    return status(c, r, 200, "OK");
}

int airplay2_handle(rtsp_instance_t *s, rtsp_client_t *c, const rtsp_request_t *r, int *handled)
{
    *handled = 1;
    if (r->method == RTSP_METHOD_GET && !strcmp(r->uri, "/info")) return info(s, c, r);
    if (r->method == RTSP_METHOD_RECORD && c->encrypted &&
        c->event_listener.handle != UINTPTR_MAX) {
        /* iOS may RECORD the control session before it supplies an audio stream.
         * PTP anchors schedule audio once the subsequent stream SETUP arrives. */
        os_mutex_lock(&s->state_lock);
        int occupied = s->stream_owner && s->stream_owner != c;
        if (s->stream_owner == c && s->stream.has_session) s->stream.recording = 1;
        os_mutex_unlock(&s->state_lock);
        if (occupied) return status(c, r, 453, "Not Enough Bandwidth");
        LOG_DEBUG("airplay2", "Control session RECORD accepted\n");
        return rtsp_send_response(c, 200, "OK", r->cseq,
                                  "Audio-Latency: 0\r\nAudio-Jack-Status: connected\r\n", NULL, 0);
    }
    if (r->method == RTSP_METHOD_POST && !strcmp(r->uri, "/fp-setup")) {
        uint8_t response[FAIRPLAY_RESPONSE_MAX];
        int size = fairplay_setup(&c->fairplay_stage, r->body, r->body_len,
                                  response, sizeof(response));
        if (size < 0) return status(c, r, 400, "Bad Request");
        LOG_DEBUG("airplay2", "FairPlay setup stage=%u request=%zu response=%d\n",
                  c->fairplay_stage, r->body_len, size);
        return rtsp_send_response(c, 200, "OK", r->cseq,
                                  "Content-Type: application/octet-stream\r\n", response, (size_t)size);
    }
    if (r->method == RTSP_METHOD_POST && !strcmp(r->uri, "/pair-setup")) {
        uint8_t response[512]; size_t size;
        if (pairing_setup(&c->pairing, r->body, r->body_len, response, sizeof(response), &size))
            return status(c, r, 400, "Bad Request");
        int result = rtsp_send_response(c, 200, "OK", r->cseq,
                                        "Content-Type: application/octet-stream\r\n", response, size);
        if (!result && c->pairing.established) {
            c->encrypted = 1;
            LOG_INFO("airplay2", "Transient pairing established with %s\n", c->peer.ip);
        }
        return result;
    }
    int binary = r->body_len >= 8 && !memcmp(r->body, "bplist00", 8);
    if ((r->method == RTSP_METHOD_SETUP && binary) ||
        r->method == RTSP_METHOD_SETRATEANCHORTIME ||
        (r->method == RTSP_METHOD_FLUSHBUFFERED && binary)) {
        if (!c->encrypted) return status(c, r, 470, "Connection Authorization Required");
        bplist_t p;
        if (bplist_open(&p, r->body, r->body_len)) return status(c, r, 400, "Bad Request");
        if (r->method == RTSP_METHOD_SETUP) return setup(s, c, r, &p);
        if (r->method == RTSP_METHOD_SETRATEANCHORTIME) return set_anchor(s, c, r, &p);
        uint64_t until, until_sequence;
        if (bplist_uint(&p, bplist_get(&p, p.root, "flushUntilTS"), &until) || until > UINT32_MAX ||
            bplist_uint(&p, bplist_get(&p, p.root, "flushUntilSeq"), &until_sequence) ||
            until_sequence > 0xffffff)
            return status(c, r, 400, "Bad Request");
        os_mutex_lock(&s->state_lock);
        if (s->stream_owner == c) {
            /* A seek may replace the RTP time base. The sequence boundary
             * identifies old buffered records without filtering the new time base. */
            s->stream.buffered_flush_sequence = (uint32_t)until_sequence;
            s->stream.has_buffered_flush_sequence = 1;
            s->stream.has_timestamp_floor = 0;
            s->stream.anchor.playing = 0;
            ++s->stream.flush_generation;
        }
        os_mutex_unlock(&s->state_lock);
        LOG_DEBUG("airplay2", "FLUSHBUFFERED through sequence=%u RTP=%u\n",
                  (uint32_t)until_sequence, (uint32_t)until);
        return status(c, r, 200, "OK");
    }
    if (r->method == RTSP_METHOD_SETPEERS || (r->method == RTSP_METHOD_POST &&
        (!strcmp(r->uri, "/feedback") || !strcmp(r->uri, "/audioMode") || !strcmp(r->uri, "/command")))) {
        if (!c->encrypted) return status(c, r, 470, "Connection Authorization Required");
        if (!strcmp(r->uri, "/feedback")) {
            uint8_t output[256]; bplist_writer_t w; uint32_t refs[4]; size_t n = 0;
            rtsp_stream_state_t stream; rtsp_get_stream_state(s, &stream);
            bplist_writer_init(&w, output, sizeof(output));
            add_int(&w, refs, &n, "type", stream.session.stream_type);
            add_int(&w, refs, &n, "sr", stream.session.sample_rate);
            uint32_t dict = bplist_add_dict(&w, refs, 2);
            uint32_t array = bplist_add_array(&w, &dict, 1);
            uint32_t root[] = {bplist_add_string(&w, "streams"), array};
            return plist_response(c, r, &w, bplist_add_dict(&w, root, 1));
        }
        return status(c, r, 200, "OK");
    }
    *handled = 0;
    return 0;
}
