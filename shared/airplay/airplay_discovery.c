#include "airplay_discovery.h"

#include <stdio.h>
#include <string.h>

static udp_socket_t g_udp_socket;
static mdns_instance_t g_raop_mdns;
static mdns_instance_t g_airplay_mdns;
static mdns_instance_t *g_instances[2] = {&g_raop_mdns, &g_airplay_mdns};
static int g_initialized = 0;
static char g_raop_instance_name[96];
static char g_airplay_instance_name[96];
static char g_host_name[96];

int airplay_discovery_init(const char *friendly_name,
                           const char *local_mac,
                           const char *local_ip,
                           const char **raop_txt_entries,
                           size_t raop_txt_count,
                           const char **airplay_txt_entries,
                           size_t airplay_txt_count)
{
    mdns_config_t raop_cfg;
    mdns_config_t airplay_cfg;

    if (!friendly_name || !local_ip)
        return -1;

    if (g_initialized)
        return 0;

    if (udp_create(&g_udp_socket, MDNS_PORT) != 0)
    {
        printf("[airplay_discovery] Failed to create UDP socket\n");
        return -1;
    }

    printf("[airplay_discovery] UDP socket created on port %d\n", MDNS_PORT);

    if (udp_join_multicast(&g_udp_socket, MDNS_MCAST_ADDR, local_ip) != 0)
    {
        printf("[airplay_discovery] Failed to join mDNS multicast group %s on interface %s\n", MDNS_MCAST_ADDR, local_ip);
        udp_close(&g_udp_socket);
        return -1;
    }

    printf("[airplay_discovery] Joined multicast group %s on interface %s\n", MDNS_MCAST_ADDR, local_ip);

    if (udp_set_multicast_interface(&g_udp_socket, local_ip) != 0)
    {
        printf("[airplay_discovery] Warning: Failed to set multicast egress interface to %s\n", local_ip);
        // Don't fail; continue anyway as fallback behavior
    }
    else
    {
        printf("[airplay_discovery] Set multicast egress interface to %s\n", local_ip);
    }

    if (local_mac && local_mac[0] != '\0')
        snprintf(g_raop_instance_name, sizeof(g_raop_instance_name), "%s@%s", local_mac, friendly_name);
    else
        snprintf(g_raop_instance_name, sizeof(g_raop_instance_name), "%s", friendly_name);

    snprintf(g_airplay_instance_name, sizeof(g_airplay_instance_name), "%s", friendly_name);

    // Keep host label simple and deterministic.
    snprintf(g_host_name, sizeof(g_host_name), "%s.local", friendly_name);

    memset(&raop_cfg, 0, sizeof(raop_cfg));
    raop_cfg.service_type = "_raop._tcp.local";
    raop_cfg.instance_name = g_raop_instance_name;
    raop_cfg.hostname = g_host_name;
    raop_cfg.port = 5000;
    raop_cfg.ipv4 = local_ip;
    raop_cfg.txt_entries = raop_txt_entries;
    raop_cfg.txt_count = (uint16_t)raop_txt_count;

    memset(&airplay_cfg, 0, sizeof(airplay_cfg));
    airplay_cfg.service_type = "_airplay._tcp.local";
    airplay_cfg.instance_name = g_airplay_instance_name;
    airplay_cfg.hostname = g_host_name;
    airplay_cfg.port = 5000;
    airplay_cfg.ipv4 = local_ip;
    airplay_cfg.txt_entries = airplay_txt_entries;
    airplay_cfg.txt_count = (uint16_t)(airplay_txt_count);

    if (mdns_create(&g_raop_mdns, &raop_cfg, &g_udp_socket) != MDNS_OK)
    {
        printf("[airplay_discovery] Failed to create RAOP mDNS instance\n");
        udp_close(&g_udp_socket);
        return -1;
    }

    if (mdns_create(&g_airplay_mdns, &airplay_cfg, &g_udp_socket) != MDNS_OK)
    {
        printf("[airplay_discovery] Failed to create AirPlay mDNS instance\n");
        udp_close(&g_udp_socket);
        return -1;
    }

    if (mdns_announce(&g_raop_mdns) != MDNS_OK)
    {
        printf("[airplay_discovery] Failed to announce RAOP service\n");
        udp_close(&g_udp_socket);
        return -1;
    }

    if (mdns_announce(&g_airplay_mdns) != MDNS_OK)
    {
        printf("[airplay_discovery] Failed to announce AirPlay service\n");
        udp_close(&g_udp_socket);
        return -1;
    }

    printf("[airplay_discovery] Both services announced successfully. Ready to respond to queries.\n");
    fflush(stdout);

    g_initialized = 1;
    return 0;
}

void airplay_discovery_run(int (*stop_fn)(void))
{
    if (!g_initialized)
        return;

    mdns_run_multiple(g_instances, 2, stop_fn);
}

void airplay_discovery_deinit(void)
{
    if (!g_initialized)
        return;

    mdns_goodbye(&g_raop_mdns);
    mdns_goodbye(&g_airplay_mdns);
    udp_close(&g_udp_socket);
    g_initialized = 0;
}
