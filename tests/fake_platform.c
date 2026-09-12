#include "fake_platform.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { LOG_ERROR("test", "fake platform: %s\n", #x); abort(); } } while (0)
int fake_resources, fake_mutexes, fake_steps, fake_io_error;
int fake_audio_writes, fake_samples;
int16_t fake_pcm[4];
airplay_server_t *fake_stop_server;
static int fail_step, packet_sent;
static unsigned char sockets[128];
static unsigned next_socket;
static uint64_t fake_time;
struct audio_device { int unused; };
static struct audio_device output;

void fake_reset(int fail)
{
    CHECK(fake_resources == 0 && fake_mutexes == 0);
    memset(sockets, 0, sizeof(sockets));
    fake_steps = fake_io_error = fake_audio_writes = fake_samples = packet_sent = 0;
    fake_stop_server = NULL;
    fail_step = fail;
    next_socket = 1;
    fake_time = UINT64_C(1700000000500000);
}
static int fails(void) { return ++fake_steps == fail_step; }
int os_mutex_init(os_mutex_t *mutex)
{
    mutex->handle = NULL;
    if (fails()) return -1;
    mutex->handle = calloc(1, sizeof(int));
    CHECK(mutex->handle);
    ++fake_mutexes;
    return 0;
}
void os_mutex_lock(os_mutex_t *mutex) { CHECK(mutex->handle && !*(int *)mutex->handle); *(int *)mutex->handle = 1; }
void os_mutex_unlock(os_mutex_t *mutex) { CHECK(mutex->handle && *(int *)mutex->handle); *(int *)mutex->handle = 0; }
void os_mutex_deinit(os_mutex_t *mutex)
{
    if (mutex->handle) { CHECK(!*(int *)mutex->handle); free(mutex->handle); mutex->handle = NULL; --fake_mutexes; }
}
void os_sleep_ms(unsigned ms) { (void)ms; }
uint64_t os_time_us(void) { return fake_time; }
int net_init(void) { return 0; }
void net_deinit(void) { }
static int open_socket(net_socket_t *socket, uint16_t port)
{
    socket->handle = UINTPTR_MAX;
    socket->port = 0;
    if (fails()) return NET_ERROR;
    CHECK(next_socket < sizeof(sockets));
    socket->handle = next_socket++;
    socket->port = port;
    sockets[socket->handle] = 1;
    ++fake_resources;
    return 0;
}
int net_tcp_listen(net_socket_t *s, const char *ip, uint16_t port) { (void)ip; return open_socket(s, port); }
int net_udp_bind(net_socket_t *s, uint16_t port) { return open_socket(s, port); }
int net_udp_join(net_socket_t *s, const char *group, const char *ip)
{ (void)s; (void)group; (void)ip; return fails() ? NET_ERROR : 0; }
void net_close(net_socket_t *s)
{
    if (s->handle != UINTPTR_MAX) {
        CHECK(s->handle < sizeof(sockets) && sockets[s->handle]);
        sockets[s->handle] = 0; --fake_resources;
        s->handle = UINTPTR_MAX; s->port = 0;
    }
}
int net_wait(const net_socket_t *s, size_t n, uint8_t *ready, int timeout)
{
    if (timeout > 0) fake_time += (unsigned)timeout * 1000;
    memset(ready, 0, n);
    if (fake_io_error) return NET_ERROR;
    if (fake_stop_server && !packet_sent) {
        for (size_t i = 0; i < n; ++i)
            if (s[i].port == AIRPLAY_AUDIO_PORT) { ready[i] = 1; return 1; }
    }
    return 0;
}
int net_tcp_accept(net_socket_t *l, net_socket_t *c, net_addr_t *p, int t)
{ (void)l; (void)c; (void)p; (void)t; return NET_TIMEOUT; }
int net_tcp_recv(net_socket_t *s, void *b, size_t n, int t)
{ (void)s; (void)b; (void)n; (void)t; return NET_TIMEOUT; }
int net_tcp_send_all(net_socket_t *s, const void *b, size_t n, int t)
{ (void)s; (void)b; (void)n; (void)t; return 0; }
int net_udp_send(net_socket_t *s, const void *b, size_t n, const net_addr_t *p)
{ (void)s; (void)b; (void)p; return fails() ? NET_ERROR : (int)n; }
int net_udp_recv(net_socket_t *s, void *b, size_t n, net_addr_t *p, int t)
{
    (void)t;
    if (fake_io_error) return NET_ERROR;
    if (s->port != AIRPLAY_AUDIO_PORT || !fake_stop_server || packet_sent) return NET_TIMEOUT;
    const uint8_t packet[] = {0x80,96,0,1,0,0,0,0,0,0,0,0, 0x03,0xe8,0xfc,0x18,0x7f,0xff,0x80,0x00};
    CHECK(n >= sizeof(packet)); memcpy(b, packet, sizeof(packet));
    if (p) { strcpy(p->ip, "127.0.0.1"); p->port = 1234; }
    packet_sent = 1;
    return sizeof(packet);
}
int audio_open(audio_device_t **d, uint32_t rate, uint8_t channels, uint8_t bits)
{
    CHECK(rate == 44100 && channels == 2 && bits == 16);
    *d = &output; ++fake_resources; return 0;
}
int audio_write(audio_device_t *d, const int16_t *samples, size_t count)
{
    CHECK(d == &output && count == 4);
    memcpy(fake_pcm, samples, sizeof(fake_pcm));
    ++fake_audio_writes; fake_samples = (int)count;
    airplay_server_stop(fake_stop_server);
    return (int)count;
}
void audio_close(audio_device_t *d) { CHECK(d == &output); --fake_resources; }
int audio_delay_frames(audio_device_t *d) { CHECK(d == &output); return 0; }
