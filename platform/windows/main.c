#include "udp_if.h"
#include "mdns.h"
#include "network_util.h"
#include "airplay/airplay_discovery.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    int result;

    const char *txt[] = {
        "txtvers=1",
        "ch=2",
        "cn=0,1,2,3",
        "da=true",
        "et=0,3,5",
        "md=0,1,2",
        "pw=false",
        "sv=false",
        "sr=44100",
        "ss=16",
        "tp=UDP",
        "vn=65537",
        "vs=130.14",
        "am=TestSpeaker",
        "sf=0x4"};

    printf("[main] Initializing AirPlay discovery (IP=10.0.0.178, MAC=1CCE516D2E30)...\n");
    fflush(stdout);

    result = airplay_discovery_init("TestSpeaker", "1CCE516D2E30", "10.0.0.178",
                                    txt, sizeof(txt) / sizeof(txt[0]),
                                    txt, sizeof(txt) / sizeof(txt[0]));
    if (result != 0)
    {
        printf("airplay_discovery_init failed: %d\n", result);
        return -1;
    }

    printf("[main] Discovery initialized successfully. Starting mDNS loop...\n");
    fflush(stdout);

    airplay_discovery_run(NULL);

    printf("[main] mDNS loop exited (should not reach here)\n");
    return 0;
}
