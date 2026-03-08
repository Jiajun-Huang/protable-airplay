/**
 * @brief mDNS multicast test
 * Tests UDP multicast and self-running mDNS server
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../shared/mdns.h"
#include "../shared/network_util.h"
#include "../platform/udp_if.h"

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#define sleep_ms(ms) Sleep(ms)
#else
#include <unistd.h>
#define sleep_ms(ms) usleep((ms) * 1000)
#endif

#define TEST_SERVICE_NAME "TestSpeaker"

static udp_socket_t g_mdns_send_sock;

void test_mdns_announcement(void)
{
    printf("\n=== Test: mDNS Platform-Driven Server ===\n");

    const char *txt_entries[] = {
        "txtvers=1",
        "ch=2",
        "cn=0",
        "tp=UDP"};

    mdns_config_t config = {
        .service_type = "_raop._tcp.local",
        .instance_name = TEST_SERVICE_NAME,
        .hostname = "testspeaker.local",
        .port = 5000,
        .ipv4 = "192.168.1.100",
        .txt_entries = txt_entries,
        .txt_count = 4};

    mdns_instance_t mdns;
    mdns_error_t result = mdns_create(&mdns, &config, &g_mdns_send_sock);
    assert(result == MDNS_OK);
    printf("[Test] ✓ mDNS instance created\n");

    result = mdns_announce(&mdns);
    assert(result == MDNS_OK);
    printf("[Test] ✓ Sent announcement\n");

    result = mdns_goodbye(&mdns);
    assert(result == MDNS_OK);
    printf("[Test] ✓ Sent goodbye\n");
}

void test_udp_multicast(void)
{
    printf("\n=== Test: UDP Multicast Socket ===\n");

    // Create UDP socket
    udp_socket_t sock;
    int result = udp_create(&sock, NET_MDNS_PORT);
    if (result != 0)
    {
        printf("[Test] ✗ Failed to create UDP socket (may need admin/root)\n");
        return;
    }
    printf("[Test] ✓ UDP socket created on port %d\n", NET_MDNS_PORT);

    // Join multicast group
    result = udp_join_multicast(&sock, NET_MDNS_MCAST_ADDR, NULL);
    if (result != 0)
    {
        printf("[Test] ✗ Failed to join multicast group (may need admin/root)\n");
        udp_close(&sock);
        return;
    }
    printf("[Test] ✓ Joined multicast group %s\n", NET_MDNS_MCAST_ADDR);

    // Listen for packets for 2 seconds
    printf("[Test] Listening for packets (2 seconds)...\n");

    uint8_t buffer[1500];
    int total_received = 0;

    for (int i = 0; i < 20; i++) // 20 x 100ms = 2 seconds
    {
        char src_ip[64];
        uint16_t src_port = 0;
        int len = udp_receive(&sock, buffer, sizeof(buffer), src_ip, &src_port, 100);
        if (len > 12)
        {
            total_received++;
            printf("[Test]   Received packet: %d bytes from %s:%u\n", len, src_ip, src_port);
        }
    }

    printf("[Test] ✓ Received %d packets\n", total_received);

    udp_close(&sock);
    printf("[Test] ✓ Socket closed\n");
}

int main(void)
{
    printf("=================================\n");
    printf("   mDNS Self-Running Test\n");
    printf("=================================\n");

    if (udp_create(&g_mdns_send_sock, 0) != 0)
    {
        printf("[Test] ✗ Failed to create mDNS send socket\n");
        return 1;
    }

    // Test mDNS announcement with platform-managed threading model
    test_mdns_announcement();

    // Optional: Test UDP multicast socket
    printf("\n[Test] Note: Multicast test may require admin/root privileges\n");
    test_udp_multicast();

    printf("\n=================================\n");
    printf("   All tests completed!\n");
    printf("=================================\n");

    udp_close(&g_mdns_send_sock);

    return 0;
}
