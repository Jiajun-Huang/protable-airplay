#include "pipeline.h"
#include "network_util.h"

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int adapter_mac_from_local_ip(const char *local_ip,
                                     char *mac_hex,
                                     size_t mac_hex_len,
                                     char *mac_colon,
                                     size_t mac_colon_len)
{
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG family = AF_INET;
    ULONG out_buf_len = 15000;
    IP_ADAPTER_ADDRESSES *addresses = NULL;
    IP_ADAPTER_ADDRESSES *adapter = NULL;
    DWORD rc;

    if (!local_ip || !mac_hex || !mac_colon || mac_hex_len < 13 || mac_colon_len < 18)
        return -1;

    addresses = (IP_ADAPTER_ADDRESSES *)malloc(out_buf_len);
    if (!addresses)
        return -1;

    rc = GetAdaptersAddresses(family, flags, NULL, addresses, &out_buf_len);
    if (rc == ERROR_BUFFER_OVERFLOW)
    {
        free(addresses);
        addresses = (IP_ADAPTER_ADDRESSES *)malloc(out_buf_len);
        if (!addresses)
            return -1;
        rc = GetAdaptersAddresses(family, flags, NULL, addresses, &out_buf_len);
    }

    if (rc != NO_ERROR)
    {
        free(addresses);
        return -1;
    }

    for (adapter = addresses; adapter; adapter = adapter->Next)
    {
        IP_ADAPTER_UNICAST_ADDRESS *ua;
        if (adapter->PhysicalAddressLength < 6)
            continue;

        for (ua = adapter->FirstUnicastAddress; ua; ua = ua->Next)
        {
            char ip_text[16];
            struct sockaddr_in *sa;

            if (!ua->Address.lpSockaddr || ua->Address.lpSockaddr->sa_family != AF_INET)
                continue;

            sa = (struct sockaddr_in *)ua->Address.lpSockaddr;
            if (InetNtopA(AF_INET, &sa->sin_addr, ip_text, sizeof(ip_text)) == NULL)
                continue;

            if (strcmp(ip_text, local_ip) != 0)
                continue;

            snprintf(mac_hex, mac_hex_len, "%02X%02X%02X%02X%02X%02X",
                     adapter->PhysicalAddress[0], adapter->PhysicalAddress[1], adapter->PhysicalAddress[2],
                     adapter->PhysicalAddress[3], adapter->PhysicalAddress[4], adapter->PhysicalAddress[5]);
            snprintf(mac_colon, mac_colon_len, "%02X:%02X:%02X:%02X:%02X:%02X",
                     adapter->PhysicalAddress[0], adapter->PhysicalAddress[1], adapter->PhysicalAddress[2],
                     adapter->PhysicalAddress[3], adapter->PhysicalAddress[4], adapter->PhysicalAddress[5]);

            free(addresses);
            return 0;
        }
    }

    free(addresses);
    return -1;
}

int main(void)
{
    pipeline_startup_t startup;

    memset(&startup, 0, sizeof(startup));
    snprintf(startup.identity.device_name, sizeof(startup.identity.device_name), "TestSpeaker");

    if (net_get_local_ipv4(startup.identity.local_ip, sizeof(startup.identity.local_ip)) != 0)
    {
        printf("[main] Failed to detect local IPv4 address\n");
        return -1;
    }

    if (adapter_mac_from_local_ip(startup.identity.local_ip,
                                  startup.identity.local_mac_hex,
                                  sizeof(startup.identity.local_mac_hex),
                                  startup.identity.local_mac_colon,
                                  sizeof(startup.identity.local_mac_colon)) != 0)
    {
        printf("[main] Failed to detect local MAC for IP %s\n", startup.identity.local_ip);
        return -1;
    }

    printf("[main] Starting pipeline (IP=%s, MAC=%s)\n",
           startup.identity.local_ip,
           startup.identity.local_mac_hex);

    return pipeline_start(&startup);
}
