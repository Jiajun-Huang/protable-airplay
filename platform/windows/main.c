#include "server.h"
#include "net.h"
#include "os.h"
#include "log.h"

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile LONG interrupted;

static BOOL WINAPI console_handler(DWORD event)
{
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT)
        return FALSE;
    InterlockedExchange(&interrupted, 1);
    return TRUE;
}

static int detect_identity(airplay_config_t *config)
{
    ULONG size = 15000;
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                  GAA_FLAG_SKIP_DNS_SERVER;
    IP_ADAPTER_ADDRESSES *addresses = NULL;
    DWORD status = ERROR_NO_DATA;
    DWORD preferred_index = 0;
    int result = -1;
    unsigned attempt;
    unsigned pass;

    for (attempt = 0; attempt < 3; ++attempt)
    {
        addresses = (IP_ADAPTER_ADDRESSES *)malloc(size);
        if (!addresses)
            return -1;
        status = GetAdaptersAddresses(AF_INET, flags, NULL, addresses, &size);
        if (status != ERROR_BUFFER_OVERFLOW)
            break;
        free(addresses);
        addresses = NULL;
    }
    if (!addresses)
        return -1;
    /* The route lookup selects an interface without sending a network packet. */
    GetBestInterface(htonl(UINT32_C(0x08080808)), &preferred_index);
    if (status == NO_ERROR)
    {
        IP_ADAPTER_ADDRESSES *adapter;
        for (pass = 0; pass < 2 && result != 0; ++pass)
        {
            for (adapter = addresses; adapter; adapter = adapter->Next)
            {
                IP_ADAPTER_UNICAST_ADDRESS *address;
                if ((pass == 0 && adapter->IfIndex != preferred_index) ||
                    adapter->OperStatus != IfOperStatusUp ||
                    adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
                    adapter->PhysicalAddressLength != 6)
                    continue;
                for (address = adapter->FirstUnicastAddress; address; address = address->Next)
                {
                    struct sockaddr_in *ipv4;
                    uint32_t host_ip;
                    if (!address->Address.lpSockaddr ||
                        address->Address.lpSockaddr->sa_family != AF_INET)
                        continue;
                    ipv4 = (struct sockaddr_in *)address->Address.lpSockaddr;
                    host_ip = ntohl(ipv4->sin_addr.s_addr);
                    if (!host_ip || (host_ip >> 24) == 127 || (host_ip >> 16) == 0xa9fe)
                        continue;
                    if (!InetNtopA(AF_INET, &ipv4->sin_addr, config->local_ip,
                                   sizeof(config->local_ip)))
                        continue;
                    snprintf(config->local_mac_hex, sizeof(config->local_mac_hex),
                             "%02X%02X%02X%02X%02X%02X",
                             adapter->PhysicalAddress[0], adapter->PhysicalAddress[1],
                             adapter->PhysicalAddress[2], adapter->PhysicalAddress[3],
                             adapter->PhysicalAddress[4], adapter->PhysicalAddress[5]);
                    result = 0;
                    break;
                }
                if (result == 0)
                    break;
            }
        }
    }
    free(addresses);
    return result;
}

static DWORD WINAPI mdns_thread(void *server)
{
    airplay_mdns_main(server);
    return 0;
}

static DWORD WINAPI rtsp_thread(void *server)
{
    airplay_rtsp_main(server);
    return 0;
}

static DWORD WINAPI audio_thread(void *server)
{
    airplay_audio_main(server);
    return 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    static airplay_server_t server;
    airplay_config_t config = {0};
    LPTHREAD_START_ROUTINE entries[] = {mdns_thread, rtsp_thread, audio_thread};
    HANDLE threads[3] = {NULL, NULL, NULL};
    unsigned started = 0;
    int result = 1;

    if (argc != 1 && argc != 3 && argc != 4)
    {
        LOG_ERROR("main", "Usage: %s [IPv4 MAC_HEX [NAME]]\n", argv[0]);
        return 1;
    }
    if (net_init() != 0)
    {
        LOG_ERROR("main", "Cannot initialize networking.\n");
        return 1;
    }
    strcpy(config.device_name, AIRPLAY_DEVICE_NAME);
    if (argc == 1)
    {
        if (detect_identity(&config) != 0)
        {
            LOG_ERROR("main", "Cannot find an active IPv4 adapter; specify IPv4 and MAC_HEX.\n");
            goto network_done;
        }
    }
    else
    {
        if (strlen(argv[1]) >= sizeof(config.local_ip) ||
            strlen(argv[2]) != 12 ||
            (argc == 4 && strlen(argv[3]) >= sizeof(config.device_name)))
        {
            LOG_ERROR("main", "Invalid IPv4, MAC_HEX, or device name length.\n");
            goto network_done;
        }
        strcpy(config.local_ip, argv[1]);
        strcpy(config.local_mac_hex, argv[2]);
        if (argc == 4)
            strcpy(config.device_name, argv[3]);
    }
    if (airplay_server_init(&server, &config) != 0)
    {
        LOG_ERROR("main", "Cannot initialize AirPlay server.\n");
        goto network_done;
    }
    if (!SetConsoleCtrlHandler(console_handler, TRUE))
    {
        LOG_ERROR("main", "Cannot register console stop handler.\n");
        goto server_done;
    }
    LOG_INFO("main", "AirPlay: %s (%s, %s). Press Ctrl+C to stop.\n",
             config.device_name, config.local_ip, config.local_mac_hex);
    for (started = 0; started < 3; ++started)
    {
        threads[started] = CreateThread(NULL, 0, entries[started], &server, 0, NULL);
        if (!threads[started])
        {
            LOG_ERROR("main", "Cannot start service thread.\n");
            break;
        }
    }
    if (started == 3)
    {
        while (!InterlockedCompareExchange(&interrupted, 0, 0) &&
               !airplay_server_is_stopping(&server))
            os_sleep_ms(50);
        result = 0;
    }
    airplay_server_stop(&server);
    while (started > 0)
    {
        --started;
        WaitForSingleObject(threads[started], INFINITE);
        CloseHandle(threads[started]);
    }
    if (airplay_server_result(&server) != 0)
        result = 1;
    SetConsoleCtrlHandler(console_handler, FALSE);
server_done:
    airplay_server_deinit(&server);
network_done:
    net_deinit();
    return result;
}
