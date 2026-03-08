#include "../../include/platform_if.h"
#include "../../core/raop.h"

int main(void)
{
    // initialize embedded variant
    raop_init(&lwip_net_if, &i2s_audio_if, &emb_platform_if);

    // TODO: lwIP init, mDNS, event loop
    while (1)
    {
        raop_process();
    }
    return 0;
}
