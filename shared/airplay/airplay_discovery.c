#include "airplay_discovery.h"
#include "airplay_config.h"
#include "network_util.h"

#include <stdio.h>
#include <string.h>

int airplay_discovery_init(airplay_discovery_t *discovery,
                           const char *friendly_name,
                           const char *local_mac,
                           const char *local_ip,
                           const char **raop_txt_entries,
                           size_t raop_txt_count)
{
    mdns_config_t config;
    uint32_t address;
    int length;
    if (!discovery || !friendly_name || !friendly_name[0] ||
        strlen(friendly_name) >= sizeof(discovery->airplay_name) || !local_ip ||
        raop_txt_count > UINT16_MAX ||
        net_str_to_ipv4(local_ip, &address) != 0 || strlen(local_ip) >= sizeof(discovery->local_ip))
        return -1;
    memset(discovery, 0, sizeof(*discovery));
    discovery->socket = (net_socket_t)NET_SOCKET_INIT;
    memcpy(discovery->local_ip, local_ip, strlen(local_ip) + 1);
    length = snprintf(discovery->raop_name, sizeof(discovery->raop_name),
                      local_mac && local_mac[0] ? "%s@%s" : "%s%s",
                      local_mac && local_mac[0] ? local_mac : "", friendly_name);
    if (length < 0 || (size_t)length >= sizeof(discovery->raop_name))
        return -1;
    length = snprintf(discovery->hostname, sizeof(discovery->hostname), "%s.local", friendly_name);
    if (length < 0 || (size_t)length >= sizeof(discovery->hostname))
        return -1;
    if (net_udp_bind(&discovery->socket, MDNS_PORT) != 0 ||
        net_udp_join(&discovery->socket, MDNS_MCAST_ADDR, local_ip) != 0)
        goto fail;

    memset(&config, 0, sizeof(config));
    config.service_type = "_raop._tcp.local";
    config.instance_name = discovery->raop_name;
    config.hostname = discovery->hostname;
    config.ipv4 = discovery->local_ip;
    config.port = AIRPLAY_RTSP_PORT;
    config.txt_entries = raop_txt_entries;
    config.txt_count = (uint16_t)raop_txt_count;
    if (mdns_create(&discovery->service, &config, &discovery->socket) != MDNS_OK)
        goto fail;
    if (mdns_announce(&discovery->service) != MDNS_OK)
        goto fail;
    strcpy(discovery->airplay_name, friendly_name);
    const char *mac = local_mac && strlen(local_mac) == 12 ? local_mac : "000000000000";
    snprintf(discovery->deviceid, sizeof(discovery->deviceid),
             "deviceid=%.2s:%.2s:%.2s:%.2s:%.2s:%.2s",
             mac, mac + 2, mac + 4, mac + 6, mac + 8, mac + 10);
    const char *airplay_txt[] = {discovery->deviceid,
        "features=" AIRPLAY2_FEATURES_TEXT, "flags=0x4", "model=" AIRPLAY_MODEL_NAME,
        "srcvers=" AIRPLAY2_SOURCE_VERSION, "vv=2", "acl=0"};
    memcpy(discovery->airplay_txt, airplay_txt, sizeof(airplay_txt));
    config.service_type = "_airplay._tcp.local";
    config.instance_name = discovery->airplay_name;
    config.txt_entries = discovery->airplay_txt;
    config.txt_count = (uint16_t)(sizeof(airplay_txt) / sizeof(airplay_txt[0]));
    if (mdns_create(&discovery->airplay_service, &config, &discovery->socket) != MDNS_OK ||
        mdns_announce(&discovery->airplay_service) != MDNS_OK)
        goto fail;
    discovery->initialized = 1;
    return 0;
fail:
    net_close(&discovery->socket);
    return -1;
}

int airplay_discovery_poll(airplay_discovery_t *discovery, int timeout_ms)
{
    uint8_t packet[1500];
    net_addr_t source;
    int length;
    if (!discovery || !discovery->initialized)
        return -1;
    length = net_udp_recv(&discovery->socket, packet, sizeof(packet), &source, timeout_ms);
    if (length == NET_TIMEOUT)
        return 0;
    if (length == NET_ERROR)
        return -1;
    mdns_error_t result = mdns_handle_packet_from(&discovery->service, packet, (size_t)length, &source);
    if (result == MDNS_ERR_SOCKET_ERROR)
        return -1;
    if (mdns_handle_packet_from(&discovery->airplay_service, packet, (size_t)length, &source)
        == MDNS_ERR_SOCKET_ERROR)
        return -1;
    return 0;
}

void airplay_discovery_deinit(airplay_discovery_t *discovery)
{
    if (!discovery || !discovery->initialized)
        return;
    mdns_goodbye(&discovery->service);
    mdns_goodbye(&discovery->airplay_service);
    net_close(&discovery->socket);
    discovery->initialized = 0;
}
